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
        bool routePlayer2JumpToPlayer1 = true;
        bool debugTelemetry = false;
        float straightness = 100.0f;
        float targetVelocity = 0.0f;
        float manualActivityWindow = 0.45f;
        float triggerCps = 5.0f;
        float assistCps = 16.0f;
        float baseDuty = 50.0f;
    };

    struct PlayerState {
        // outputPressed is the effective button state currently sent to GD.
        // Before activation this follows the physical key. In cadence mode it
        // follows the autonomous pulse generator instead.
        bool outputPressed = false;
        bool manualHeld = false;
        bool cadenceModeActive = false;
        bool controllerOwned = false;

        float localClock = 0.0f;
        float activityTimer = 0.0f;
        float cyclePhase = 0.0f;
        float currentDuty = 0.50f;
        float velocityIntegral = 0.0f;
        float telemetryTimer = 0.0f;

        float lastManualPressAt = -1.0f;
        float manualCadenceHz = 0.0f;
        int qualifyingIntervals = 0;

        float lastGravityRelativeVelocity = 0.0f;
        float estimatedAcceleration = 0.0f;
        uint32_t physicsHookCalls = 0;
        uint32_t manualInputEvents = 0;
        bool hasVelocitySample = false;
        bool lastUpsideDown = false;
    };

    Config g_config;
    std::array<PlayerState, 2> g_players{};
    PlayLayer* g_activeLayer = nullptr;
    bool g_injectingInput = false;

    float clampActivityWindow() {
        return std::clamp(g_config.manualActivityWindow, 0.10f, 1.50f);
    }

    float triggerCps() {
        return std::clamp(g_config.triggerCps, 2.0f, 10.0f);
    }

    float assistCps() {
        // Keep a practical ceiling for common physics update rates. At 60 Hz,
        // 30 complete press/release cycles per second is already the Nyquist-ish
        // upper bound before individual pulse edges become undersampled.
        return std::clamp(g_config.assistCps, 1.0f, 30.0f);
    }

    float straightness01() {
        return std::clamp(g_config.straightness, 0.0f, 100.0f) / 100.0f;
    }

    float gravityRelativeVelocity(PlayerObject* player) {
        if (!player) return 0.0f;
        // Positive velocity always means movement in the direction of gravity.
        const float gravitySign = player->m_isUpsideDown ? 1.0f : -1.0f;
        return static_cast<float>(player->m_yVelocity) * gravitySign;
    }

    void sendControllerInput(PlayLayer* layer, bool down, bool isPlayer1) {
        if (!layer) return;
        const bool previousInjecting = g_injectingInput;
        g_injectingInput = true;
        layer->handleButton(down, 1, isPlayer1);
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
    }

    void relinquishController(PlayLayer* layer, PlayerState& state, bool isPlayer1, bool preserveManualHold) {
        const bool restorePhysicalHold = preserveManualHold && state.manualHeld;
        releaseControllerOutput(layer, state, isPlayer1);
        state.cadenceModeActive = false;
        state.controllerOwned = false;
        state.activityTimer = 0.0f;
        state.cyclePhase = 0.0f;
        state.currentDuty = std::clamp(g_config.baseDuty / 100.0f, 0.20f, 0.80f);
        state.velocityIntegral = 0.0f;
        state.manualCadenceHz = 0.0f;
        state.qualifyingIntervals = 0;
        state.lastManualPressAt = -1.0f;
        state.hasVelocitySample = false;
        state.estimatedAcceleration = 0.0f;

        // If the assist is disabled or Ship mode ends while the key is still
        // physically held, hand that hold back to the native game input.
        if (restorePhysicalHold && layer) {
            sendControllerInput(layer, true, isPlayer1);
        }
        if (!preserveManualHold) state.manualHeld = false;
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
        config.routePlayer2JumpToPlayer1 = mod->getSettingValue<bool>("route-player2-jump-to-player1");
        config.debugTelemetry = mod->getSettingValue<bool>("debug-telemetry");
        config.straightness = static_cast<float>(mod->getSettingValue<double>("straightness"));
        config.targetVelocity = static_cast<float>(mod->getSettingValue<double>("target-vertical-velocity"));
        config.manualActivityWindow = static_cast<float>(mod->getSettingValue<double>("manual-activity-window"));
        config.triggerCps = static_cast<float>(mod->getSettingValue<double>("trigger-cps"));
        config.assistCps = static_cast<float>(mod->getSettingValue<double>("assist-cps"));
        config.baseDuty = static_cast<float>(mod->getSettingValue<double>("base-duty"));
        return config;
    }

    // Track press-edge cadence. Two consecutive intervals above the threshold
    // are required, so a single accidental fast pair does not switch modes.
    void noteManualInput(PlayerState& state, bool down) {
        if (down == state.manualHeld) return;

        if (down) {
            if (state.lastManualPressAt >= 0.0f) {
                const float interval = state.localClock - state.lastManualPressAt;
                if (interval >= 0.008f && interval <= 1.0f) {
                    const float measuredHz = std::clamp(1.0f / interval, 0.0f, 120.0f);
                    if (state.manualCadenceHz <= 0.0f) {
                        state.manualCadenceHz = measuredHz;
                    } else {
                        state.manualCadenceHz = state.manualCadenceHz * 0.45f + measuredHz * 0.55f;
                    }
                    // Allow a small timing quantization margin because the
                    // game's input edges are observed on physics/frame ticks.
                    if (measuredHz >= triggerCps() * 0.90f) {
                        ++state.qualifyingIntervals;
                    } else {
                        state.qualifyingIntervals = 0;
                    }
                } else {
                    state.qualifyingIntervals = 0;
                    if (interval > 1.0f) state.manualCadenceHz = 0.0f;
                }
            }
            state.lastManualPressAt = state.localClock;
            state.manualHeld = true;
        } else {
            state.manualHeld = false;
        }

        if (state.cadenceModeActive) {
            state.activityTimer = clampActivityWindow();
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
            // Ignore abrupt changes caused by portals, collisions or resets.
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

    float calculateDutyTarget(PlayerObject* player, PlayerState& state, float dt) {
        const float baseDuty = std::clamp(g_config.baseDuty / 100.0f, 0.20f, 0.80f);
        const float strength = straightness01();
        const float velocity = gravityRelativeVelocity(player);
        const float predictedVelocity = velocity + state.estimatedAcceleration * 0.010f;
        const float error = predictedVelocity - g_config.targetVelocity;

        // A fixed cycle rate is maintained; only the press portion of each
        // cycle changes. More downward drift increases thrust duty, and upward
        // drift reduces it. Straightness controls feedback aggressiveness.
        state.velocityIntegral = std::clamp(
            state.velocityIntegral + error * std::clamp(dt, 0.0f, 0.05f),
            -2.5f,
            2.5f
        );
        const float kp = 0.055f + 0.225f * strength;
        const float ki = 0.008f + 0.035f * strength;
        const float correction = kp * error + ki * state.velocityIntegral;
        return std::clamp(baseDuty + correction, 0.08f, 0.92f);
    }

    void activateCadenceMode(PlayerState& state) {
        state.cadenceModeActive = true;
        state.controllerOwned = true;
        state.activityTimer = clampActivityWindow();
        state.cyclePhase = 0.0f;
        state.currentDuty = std::clamp(g_config.baseDuty / 100.0f, 0.20f, 0.80f);
        state.velocityIntegral = 0.0f;
        // The press edge that crossed the threshold is intentionally NOT
        // forwarded. The prior native press was already released by the user.
    }

    void runControllerBeforePhysics(PlayerObject* player, PlayerState& state, bool selected, bool isPlayer1, float dt) {
        auto* layer = g_activeLayer;
        if (!player || !layer) return;

        const float safeDt = std::clamp(dt, 0.0f, 0.05f);
        state.localClock += safeDt;
        state.activityTimer = std::max(0.0f, state.activityTimer - safeDt);
        state.telemetryTimer = std::max(0.0f, state.telemetryTimer - safeDt);

        if (player->m_isDead) {
            if (state.cadenceModeActive) relinquishController(layer, state, isPlayer1, false);
            state.manualHeld = false;
            state.activityTimer = 0.0f;
            state.manualCadenceHz = 0.0f;
            state.qualifyingIntervals = 0;
            state.lastManualPressAt = -1.0f;
            return;
        }

        if (!g_config.enabled || !selected || !player->m_isShip) {
            if (state.cadenceModeActive) relinquishController(layer, state, isPlayer1, true);
            state.hasVelocitySample = false;
            return;
        }

        // Until the user reaches the manual CPS threshold, all input is passed
        // through untouched and the Ship behaves exactly like vanilla GD.
        if (!state.cadenceModeActive) return;

        // Keep the fixed cadence active while the key is held or until the
        // activity window expires after the last physical press/release.
        if (!state.manualHeld && state.activityTimer <= 0.0f) {
            relinquishController(layer, state, isPlayer1, false);
            return;
        }

        state.controllerOwned = true;
        // Update feedback every physics step, but only apply the resulting duty
        // adjustment at a cycle boundary. This preserves a constant CPS clock.
        const float targetDuty = calculateDutyTarget(player, state, safeDt);
        const float phaseAdvance = safeDt * assistCps();
        state.cyclePhase += phaseAdvance;
        if (state.cyclePhase >= 1.0f) {
            state.cyclePhase -= std::floor(state.cyclePhase);
            state.currentDuty += (targetDuty - state.currentDuty) * 0.45f;
        }

        // A complete cycle contains one press interval and one release interval.
        // Manual clicks no longer determine these transitions once thresholded.
        const bool desiredPressed = state.cyclePhase < state.currentDuty;
        setControllerOutput(layer, state, desiredPressed, isPlayer1);

        if (g_config.debugTelemetry && state.telemetryTimer <= 0.0f) {
            log::info(
                "AA {}: mode=CADENCE, manualCPS={:.2f}, trigger={:.2f}, assistCPS={:.2f}, phase={:.3f}, duty={:.3f}, vy={:.4f}, integral={:.3f}, output={}, armed={}, manualHeld={}, physicsCalls={}, manualEvents={}",
                isPlayer1 ? "P1" : "P2",
                state.manualCadenceHz,
                triggerCps(),
                assistCps(),
                state.cyclePhase,
                state.currentDuty,
                gravityRelativeVelocity(player),
                state.velocityIntegral,
                state.outputPressed,
                state.cadenceModeActive,
                state.manualHeld,
                state.physicsHookCalls,
                state.manualInputEvents
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

        if (!isActivePlayLayer || button != 1) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        // Synthetic cadence edges always pass to the original game path.
        if (alternate_assist::g_injectingInput) {
            auto& injectedState = alternate_assist::g_players[isPlayer1 ? 0u : 1u];
            injectedState.outputPressed = down;
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        // Geode's common keyboard defaults can bind Up Arrow to P2 jump.
        // In single-player only, route that input to P1 so the user's arrow key
        // remains a valid Ship control. Dual mode keeps P2 independent.
        const bool remapP2ToP1 = !isPlayer1 &&
            alternate_assist::g_config.routePlayer2JumpToPlayer1 &&
            !activeLayer->m_gameState.m_isDualMode;
        const bool effectivePlayer1 = isPlayer1 || remapP2ToP1;
        auto& state = alternate_assist::g_players[effectivePlayer1 ? 0u : 1u];
        auto* player = effectivePlayer1 ? activeLayer->m_player1 : activeLayer->m_player2;
        const bool selected = effectivePlayer1 || alternate_assist::g_config.player2Enabled;
        const bool isShipCandidate = alternate_assist::g_config.enabled && selected &&
            player && player->m_isShip && !player->m_isDead;

        if (!isShipCandidate) {
            if (state.cadenceModeActive) {
                alternate_assist::relinquishController(activeLayer, state, effectivePlayer1, false);
            }
            state.manualHeld = down;
            state.outputPressed = down;
            GJBaseGameLayer::handleButton(down, button, effectivePlayer1);
            return;
        }

        alternate_assist::noteManualInput(state, down);
        ++state.manualInputEvents;

        if (alternate_assist::g_config.debugTelemetry && down) {
            log::info(
                "AA input {}: manualCPS={:.2f}, triggerCPS={:.2f}, qualifyingIntervals={}, cadenceActive={}, physicsCalls={}, events={}",
                effectivePlayer1 ? "P1" : "P2",
                state.manualCadenceHz,
                alternate_assist::triggerCps(),
                state.qualifyingIntervals,
                state.cadenceModeActive,
                state.physicsHookCalls,
                state.manualInputEvents
            );
        }

        if (state.cadenceModeActive) {
            // Once active, physical clicks only keep the cadence mode armed.
            // They cannot speed up, slow down, or phase-lock the generated CPS.
            state.activityTimer = alternate_assist::clampActivityWindow();
            return;
        }

        if (down && state.qualifyingIntervals >= 2 &&
            state.manualCadenceHz >= alternate_assist::triggerCps() * 0.90f) {
            // The current physical edge is replaced by the cadence controller.
            // All earlier sub-threshold clicks/releases were forwarded normally.
            alternate_assist::activateCadenceMode(state);
            return;
        }

        // Below threshold, native input always reaches GD and other mods. This
        // prevents the Ship from becoming unusable when assist is merely enabled.
        state.outputPressed = down;
        GJBaseGameLayer::handleButton(down, button, effectivePlayer1);
    }
};

class $modify(AlternateAssistPlayerObject, PlayerObject) {
    void updateJump(float dt) {
        auto* layer = alternate_assist::g_activeLayer;
        if (layer && this == layer->m_player1) {
            ++alternate_assist::g_players[0].physicsHookCalls;
            alternate_assist::runControllerBeforePhysics(
                this, alternate_assist::g_players[0], true, true, dt
            );
            PlayerObject::updateJump(dt);
            alternate_assist::observePostPhysics(this, alternate_assist::g_players[0], dt);
            return;
        }
        if (layer && this == layer->m_player2) {
            ++alternate_assist::g_players[1].physicsHookCalls;
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
        alternate_assist::g_config.enabled = value;
        if (!value && alternate_assist::g_activeLayer) {
            for (std::size_t i = 0; i < alternate_assist::g_players.size(); ++i) {
                auto& state = alternate_assist::g_players[i];
                if (state.cadenceModeActive) {
                    alternate_assist::relinquishController(
                        alternate_assist::g_activeLayer, state, i == 0, true
                    );
                }
            }
        }
    });

    listenForSettingChanges<bool>("player2-enabled", [](bool value) {
        alternate_assist::g_config.player2Enabled = value;
        if (!value && alternate_assist::g_activeLayer && alternate_assist::g_players[1].cadenceModeActive) {
            alternate_assist::relinquishController(
                alternate_assist::g_activeLayer, alternate_assist::g_players[1], false, true
            );
        }
    });

    listenForSettingChanges<bool>("route-player2-jump-to-player1", [](bool value) {
        alternate_assist::g_config.routePlayer2JumpToPlayer1 = value;
    });

    listenForSettingChanges<bool>("debug-telemetry", [](bool value) {
        alternate_assist::g_config.debugTelemetry = value;
    });

    listenForSettingChanges<double>("straightness", [](double value) {
        alternate_assist::g_config.straightness = static_cast<float>(value);
    });

    listenForSettingChanges<double>("trigger-cps", [](double value) {
        alternate_assist::g_config.triggerCps = static_cast<float>(value);
    });

    listenForSettingChanges<double>("assist-cps", [](double value) {
        alternate_assist::g_config.assistCps = static_cast<float>(value);
    });

    listenForSettingChanges<double>("base-duty", [](double value) {
        alternate_assist::g_config.baseDuty = static_cast<float>(value);
    });

    listenForSettingChanges<double>("target-vertical-velocity", [](double value) {
        alternate_assist::g_config.targetVelocity = static_cast<float>(value);
    });

    listenForSettingChanges<double>("manual-activity-window", [](double value) {
        alternate_assist::g_config.manualActivityWindow = static_cast<float>(value);
    });

    log::info("Alternate Assist Mod v0.1.7 loaded. Native input is preserved below the manual CPS threshold; above threshold it switches to independent fixed-cadence Ship pulses with feedback duty correction.");
}
