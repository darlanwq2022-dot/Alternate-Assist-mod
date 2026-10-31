#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using namespace geode::prelude;

namespace alternate_assist {
    struct Config {
        bool enabled = false;
        bool player2Enabled = false;
        bool debugTelemetry = false;
        float straightness = 95.0f;
        float targetVelocity = 0.0f;
        float resumeDelay = 0.0f;
        float manualActivityWindow = 0.32f;
        float inputAlignment = 1.0f;
        int maxCps = 120;
    };

    struct PlayerState {
        // outputPressed is the button state currently sent to Geometry Dash.
        // manualHeld only records the user's physical hold; it does not force
        // thrust. The controller chooses the final button state from velocity.
        bool outputPressed = false;
        bool manualHeld = false;
        bool controllerOwned = false;

        float localClock = 0.0f;
        float activityTimer = 0.0f;
        float resumeTimer = 0.0f;
        float transitionCooldown = 0.0f;
        float telemetryTimer = 0.0f;

        float lastManualPressAt = -1.0f;
        float manualCadenceHz = 0.0f;

        float lastGravityRelativeVelocity = 0.0f;
        float estimatedAcceleration = 0.0f;
        bool hasVelocitySample = false;
        bool lastUpsideDown = false;
    };

    Config g_config;
    std::array<PlayerState, 2> g_players{};
    PlayLayer* g_activeLayer = nullptr;
    bool g_injectingInput = false;
    bool g_restoringManualInput = false;

    float straightness01() {
        return std::clamp(g_config.straightness, 0.0f, 100.0f) / 100.0f;
    }

    int maxCps() {
        return std::clamp(g_config.maxCps, 1, 120);
    }

    float activityWindow() {
        return std::clamp(g_config.manualActivityWindow, 0.05f, 1.0f);
    }

    float gravityRelativeVelocity(PlayerObject* player) {
        if (!player) return 0.0f;
        // In normal gravity negative Y is falling; inverted gravity reverses it.
        // Positive therefore always means motion in the direction of gravity.
        const float gravitySign = player->m_isUpsideDown ? 1.0f : -1.0f;
        return static_cast<float>(player->m_yVelocity) * gravitySign;
    }

    float baseVelocityDeadzone() {
        // Direct Straightness mapping:
        //   0   -> forgiving threshold (~1.18)
        //   50  -> medium threshold (~0.47)
        //   90  -> tight threshold (~0.08)
        //   100 -> very tight threshold (0.03)
        const float remaining = 1.0f - straightness01();
        return 0.03f + 1.15f * std::pow(remaining, 1.4f);
    }

    float effectiveVelocityDeadzone(PlayerState const& state) {
        const float cadence = std::clamp(state.manualCadenceHz / 30.0f, 0.0f, 1.0f);
        const float alignment = std::clamp(g_config.inputAlignment, 0.0f, 1.0f);
        // Faster real clicks tighten the threshold by up to 42%.
        const float cadenceTightening = 0.42f * alignment * cadence;
        return std::max(0.012f, baseVelocityDeadzone() * (1.0f - cadenceTightening));
    }

    void sendControllerInput(PlayLayer* layer, bool down, bool isPlayer1) {
        if (!layer) return;
        const bool previousInjecting = g_injectingInput;
        const bool previousRestoring = g_restoringManualInput;
        g_injectingInput = true;
        g_restoringManualInput = false;
        layer->handleButton(down, 1, isPlayer1);
        g_restoringManualInput = previousRestoring;
        g_injectingInput = previousInjecting;
    }

    void restoreManualHeldInput(PlayLayer* layer, bool isPlayer1) {
        if (!layer) return;
        const bool previousInjecting = g_injectingInput;
        const bool previousRestoring = g_restoringManualInput;
        g_injectingInput = true;
        g_restoringManualInput = true;
        layer->handleButton(true, 1, isPlayer1);
        g_restoringManualInput = previousRestoring;
        g_injectingInput = previousInjecting;
    }

    void setControllerOutput(PlayLayer* layer, PlayerState& state, bool down, bool isPlayer1) {
        if (!layer || state.outputPressed == down) return;
        sendControllerInput(layer, down, isPlayer1);
    }

    void releaseControllerOutput(PlayLayer* layer, PlayerState& state, bool isPlayer1) {
        if (layer && state.outputPressed) {
            sendControllerInput(layer, false, isPlayer1);
        } else if (!layer) {
            state.outputPressed = false;
        }
        state.transitionCooldown = 0.0f;
    }

    void relinquishController(PlayLayer* layer, PlayerState& state, bool isPlayer1, bool restoreHeldInput) {
        const bool wasManuallyHeld = state.manualHeld;
        releaseControllerOutput(layer, state, isPlayer1);
        state.controllerOwned = false;
        state.activityTimer = 0.0f;
        state.resumeTimer = 0.0f;
        state.manualCadenceHz = 0.0f;
        state.lastManualPressAt = -1.0f;
        state.hasVelocitySample = false;
        state.estimatedAcceleration = 0.0f;

        // If assist is disabled or a portal leaves Ship mode during a physical
        // hold, restore the user's normal held input instead of swallowing it.
        if (restoreHeldInput && wasManuallyHeld && layer) {
            // Restore the physical hold to vanilla gameplay, but do not mark it
            // as controller-owned output. This prevents a per-physics-step
            // release/restore loop after leaving Ship mode or disabling assist.
            restoreManualHeldInput(layer, isPlayer1);
        } else if (!restoreHeldInput) {
            state.manualHeld = false;
        }
    }

    void resetPlayerStates() {
        g_players = {};
    }

    void releaseAllControllerOutputs(PlayLayer* layer) {
        releaseControllerOutput(layer, g_players[0], true);
        releaseControllerOutput(layer, g_players[1], false);
    }

    Config readConfig() {
        auto* mod = Mod::get();
        Config config;
        config.enabled = mod->getSettingValue<bool>("assist-enabled");
        config.player2Enabled = mod->getSettingValue<bool>("player2-enabled");
        config.debugTelemetry = mod->getSettingValue<bool>("debug-telemetry");
        config.straightness = static_cast<float>(mod->getSettingValue<double>("straightness"));
        config.targetVelocity = static_cast<float>(mod->getSettingValue<double>("target-vertical-velocity"));
        config.resumeDelay = static_cast<float>(mod->getSettingValue<double>("manual-resume-delay"));
        config.manualActivityWindow = static_cast<float>(mod->getSettingValue<double>("manual-activity-window"));
        config.inputAlignment = static_cast<float>(mod->getSettingValue<double>("input-alignment"));
        config.maxCps = static_cast<int>(mod->getSettingValue<int64_t>("max-cps"));
        return config;
    }

    void noteManualInput(PlayerState& state, bool down) {
        // Ignore duplicate edges; only a fresh physical down edge counts as a click.
        if (down == state.manualHeld) return;

        if (down) {
            if (state.lastManualPressAt >= 0.0f) {
                const float interval = state.localClock - state.lastManualPressAt;
                if (interval >= 0.008f && interval <= 1.0f) {
                    const float measuredHz = std::clamp(1.0f / interval, 1.0f, 120.0f);
                    if (state.manualCadenceHz <= 0.0f) {
                        state.manualCadenceHz = measuredHz;
                    } else {
                        state.manualCadenceHz = state.manualCadenceHz * 0.55f + measuredHz * 0.45f;
                    }
                } else if (interval > 1.0f) {
                    state.manualCadenceHz = 0.0f;
                }
            }
            state.lastManualPressAt = state.localClock;
            state.manualHeld = true;
            state.activityTimer = activityWindow();
            state.resumeTimer = 0.0f;
            state.controllerOwned = true;
        } else {
            state.manualHeld = false;
            // Keep the stabilizer armed briefly after the last click. Repeated
            // clicks continually refresh the window; idle eventually disengages.
            state.activityTimer = std::max(state.activityTimer, activityWindow());
            state.resumeTimer = std::clamp(g_config.resumeDelay, 0.0f, 0.20f);
        }
    }

    void observePostPhysics(PlayerObject* player, PlayerState& state, float dt) {
        if (!player || !player->m_isShip || player->m_isDead) {
            state.hasVelocitySample = false;
            state.estimatedAcceleration = 0.0f;
            return;
        }

        const float safeDt = std::clamp(dt, 0.0f, 0.05f);
        if (safeDt <= 0.00001f) return;

        const bool upsideDown = player->m_isUpsideDown;
        const float velocity = gravityRelativeVelocity(player);
        if (state.hasVelocitySample && upsideDown == state.lastUpsideDown) {
            const float rawAcceleration = (velocity - state.lastGravityRelativeVelocity) / safeDt;
            // Discard discontinuities from portals, collisions and velocity resets.
            if (std::fabs(rawAcceleration) <= 36.0f) {
                const float alpha = std::clamp(safeDt * 24.0f, 0.04f, 0.40f);
                state.estimatedAcceleration += (rawAcceleration - state.estimatedAcceleration) * alpha;
            } else {
                state.estimatedAcceleration = 0.0f;
            }
        } else {
            state.estimatedAcceleration = 0.0f;
        }

        state.lastGravityRelativeVelocity = velocity;
        state.lastUpsideDown = upsideDown;
        state.hasVelocitySample = true;
    }

    void runControllerBeforePhysics(PlayerObject* player, PlayerState& state, bool selected, bool isPlayer1, float dt) {
        auto* layer = g_activeLayer;
        if (!player || !layer) return;

        const float safeDt = std::clamp(dt, 0.0f, 0.05f);
        state.localClock += safeDt;
        state.activityTimer = std::max(0.0f, state.activityTimer - safeDt);
        state.resumeTimer = std::max(0.0f, state.resumeTimer - safeDt);
        state.transitionCooldown = std::max(0.0f, state.transitionCooldown - safeDt);
        state.telemetryTimer = std::max(0.0f, state.telemetryTimer - safeDt);

        if (player->m_isDead) {
            if (state.controllerOwned) {
                relinquishController(layer, state, isPlayer1, false);
            }
            state.manualHeld = false;
            state.activityTimer = 0.0f;
            state.manualCadenceHz = 0.0f;
            state.lastManualPressAt = -1.0f;
            return;
        }

        if (!g_config.enabled || !selected || !player->m_isShip) {
            if (state.controllerOwned) {
                relinquishController(layer, state, isPlayer1, true);
            }
            state.hasVelocitySample = false;
            return;
        }

        const bool armed = state.manualHeld || state.activityTimer > 0.0f;
        if (!armed) {
            if (state.controllerOwned) {
                relinquishController(layer, state, isPlayer1, false);
            }
            if (state.lastManualPressAt >= 0.0f && state.localClock - state.lastManualPressAt > 1.0f) {
                state.manualCadenceHz = 0.0f;
                state.lastManualPressAt = -1.0f;
            }
            state.hasVelocitySample = false;
            return;
        }

        state.controllerOwned = true;
        if (state.resumeTimer > 0.0f) return;

        const float straightness = straightness01();
        const float cadenceFactor = std::clamp(state.manualCadenceHz / 30.0f, 0.0f, 1.0f);
        const float alignment = std::clamp(g_config.inputAlignment, 0.0f, 1.0f);
        const float deadzone = effectiveVelocityDeadzone(state);
        const float releaseZone = std::max(0.008f, deadzone * 0.62f);

        // Predict a few physics ticks ahead. The prediction horizon grows with
        // Straightness and recent click cadence, while the measured velocity
        // remains the primary feedback signal.
        const float lookahead = std::clamp(
            0.003f + 0.018f * straightness + 0.010f * alignment * cadenceFactor,
            0.003f,
            0.032f
        );
        const float velocity = gravityRelativeVelocity(player);
        const float predictedVelocity = velocity + state.estimatedAcceleration * lookahead;
        const float error = predictedVelocity - g_config.targetVelocity;

        // Bang-bang stabilization with hysteresis: press while drifting with
        // gravity; release after momentum has crossed the opposite threshold.
        bool desiredPressed = state.outputPressed;
        if (state.outputPressed) {
            if (error < -releaseZone) desiredPressed = false;
        } else if (error > deadzone) {
            desiredPressed = true;
        }

        if (desiredPressed != state.outputPressed && state.transitionCooldown <= 0.0f) {
            setControllerOutput(layer, state, desiredPressed, isPlayer1);
            // max CPS means complete press+release cycles, so each edge gets
            // half of the cycle interval. Physics still limits this to one edge
            // per updateJump call.
            state.transitionCooldown = 1.0f / (2.0f * static_cast<float>(maxCps()));
        }

        if (g_config.debugTelemetry && state.telemetryTimer <= 0.0f) {
            log::info(
                "AA {}: vy={:.4f}, predicted={:.4f}, deadzone={:.4f}, output={}, manualHz={:.1f}, straightness={:.0f}%, armed={}, accel={:.3f}",
                isPlayer1 ? "P1" : "P2",
                velocity,
                predictedVelocity,
                deadzone,
                state.outputPressed,
                state.manualCadenceHz,
                g_config.straightness,
                armed,
                state.estimatedAcceleration
            );
            state.telemetryTimer = 0.20f;
        }
    }
}

class $modify(AlternateAssistPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        alternate_assist::g_activeLayer = nullptr;
        alternate_assist::resetPlayerStates();
        const bool result = PlayLayer::init(level, useReplay, dontCreateObjects);
        if (result) alternate_assist::g_activeLayer = this;
        return result;
    }

    void resetLevel() {
        alternate_assist::releaseAllControllerOutputs(this);
        alternate_assist::resetPlayerStates();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        alternate_assist::releaseAllControllerOutputs(this);
        alternate_assist::resetPlayerStates();
        alternate_assist::g_activeLayer = nullptr;
        PlayLayer::onQuit();
    }
};

class $modify(AlternateAssistBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto* activeLayer = alternate_assist::g_activeLayer;
        const bool isActivePlayLayer = activeLayer &&
            static_cast<GJBaseGameLayer*>(activeLayer) == this;

        if (button != 1 || !isActivePlayLayer) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto& state = alternate_assist::g_players[isPlayer1 ? 0u : 1u];
        if (alternate_assist::g_injectingInput) {
            if (!alternate_assist::g_restoringManualInput) {
                state.outputPressed = down;
            }
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto* player = isPlayer1 ? activeLayer->m_player1 : activeLayer->m_player2;
        const bool selected = isPlayer1 || alternate_assist::g_config.player2Enabled;
        const bool captureForAssist = alternate_assist::g_config.enabled && selected &&
            player && player->m_isShip && !player->m_isDead;

        if (captureForAssist) {
            // While assisted Ship control is active, physical edges arm the
            // stabilizer and contribute cadence data. They are not forwarded as
            // raw holds: otherwise a long manual hold would prevent the
            // controller from releasing thrust to counter upward drift.
            alternate_assist::noteManualInput(state, down);
            state.controllerOwned = true;
            return;
        }

        if (state.controllerOwned) {
            alternate_assist::relinquishController(activeLayer, state, isPlayer1, false);
        }
        state.manualHeld = down;
        // Raw gameplay input is not controller-owned output.
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(AlternateAssistPlayerObject, PlayerObject) {
    void updateJump(float dt) {
        auto* layer = alternate_assist::g_activeLayer;
        if (layer && this == layer->m_player1) {
            alternate_assist::runControllerBeforePhysics(
                this, alternate_assist::g_players[0], true, true, dt
            );
            PlayerObject::updateJump(dt);
            alternate_assist::observePostPhysics(this, alternate_assist::g_players[0], dt);
            return;
        }
        if (layer && this == layer->m_player2) {
            alternate_assist::runControllerBeforePhysics(
                this,
                alternate_assist::g_players[1],
                alternate_assist::g_config.player2Enabled,
                false,
                dt
            );
            PlayerObject::updateJump(dt);
            alternate_assist::observePostPhysics(this, alternate_assist::g_players[1], dt);
            return;
        }
        PlayerObject::updateJump(dt);
    }
};

$on_mod(Loaded) {
    alternate_assist::g_config = alternate_assist::readConfig();

    listenForSettingChanges<bool>("assist-enabled", [](bool value) {
        const bool wasEnabled = alternate_assist::g_config.enabled;
        alternate_assist::g_config.enabled = value;
        if (wasEnabled && !value) {
            auto* layer = alternate_assist::g_activeLayer;
            for (std::size_t i = 0; i < alternate_assist::g_players.size(); ++i) {
                auto& state = alternate_assist::g_players[i];
                if (state.controllerOwned) {
                    alternate_assist::relinquishController(layer, state, i == 0, true);
                }
            }
        }
    });

    listenForSettingChanges<bool>("player2-enabled", [](bool value) {
        alternate_assist::g_config.player2Enabled = value;
        if (!value && alternate_assist::g_activeLayer) {
            if (alternate_assist::g_players[1].controllerOwned) {
                alternate_assist::relinquishController(
                    alternate_assist::g_activeLayer,
                    alternate_assist::g_players[1],
                    false,
                    true
                );
            }
        }
    });

    listenForSettingChanges<bool>("debug-telemetry", [](bool value) {
        alternate_assist::g_config.debugTelemetry = value;
    });

    listenForSettingChanges<double>("straightness", [](double value) {
        alternate_assist::g_config.straightness = static_cast<float>(value);
    });

    listenForSettingChanges<int64_t>("max-cps", [](int64_t value) {
        alternate_assist::g_config.maxCps = static_cast<int>(value);
    });

    listenForSettingChanges<double>("target-vertical-velocity", [](double value) {
        alternate_assist::g_config.targetVelocity = static_cast<float>(value);
    });

    listenForSettingChanges<double>("manual-resume-delay", [](double value) {
        alternate_assist::g_config.resumeDelay = static_cast<float>(value);
    });

    listenForSettingChanges<double>("manual-activity-window", [](double value) {
        alternate_assist::g_config.manualActivityWindow = static_cast<float>(value);
    });

    listenForSettingChanges<double>("input-alignment", [](double value) {
        alternate_assist::g_config.inputAlignment = static_cast<float>(value);
    });

    log::info("Alternate Assist Mod v0.1.3 loaded. Adaptive Ship stabilizer with manual cadence alignment.");
}
