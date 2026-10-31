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
        int maxCps = 30;
    };

    struct PlayerState {
        // The legacy controller is state-based: it presses while the Ship is
        // drifting with gravity and releases while it is drifting against it.
        bool autoPressed = false;
        bool manualHeld = false;
        bool assistActive = false;

        float localClock = 0.0f;
        float activityTimer = 0.0f;
        float toggleTimer = 0.0f;
        float telemetryTimer = 0.0f;

        float lastManualPressAt = -1.0f;
        float manualCps = 0.0f;
        int qualifyingIntervals = 0;
        uint32_t manualInputEvents = 0;
        uint32_t playerUpdateCalls = 0;
    };

    Config g_config;
    std::array<PlayerState, 2> g_players{};
    PlayLayer* g_activeLayer = nullptr;
    bool g_injectingInput = false;

    float safeActivityWindow() {
        return std::clamp(g_config.manualActivityWindow, 0.10f, 1.50f);
    }

    float triggerCps() {
        return std::clamp(g_config.triggerCps, 2.0f, 10.0f);
    }

    int maxCps() {
        return std::clamp(g_config.maxCps, 1, 60);
    }

    float deadzoneForStraightness() {
        // Reuse the original v0.1.0 Extreme-Straight response at 100%.
        // Lower straightness widens the velocity band and reduces corrections.
        const float strength = std::clamp(g_config.straightness, 0.0f, 100.0f) / 100.0f;
        return 1.35f - (1.35f - 0.18f) * strength;
    }

    float gravityRelativeVelocity(PlayerObject* player) {
        if (!player) return 0.0f;
        // Positive = moving in the direction of gravity; this keeps the same
        // controller rule valid for normal and upside-down gravity.
        const float gravitySign = player->m_isUpsideDown ? 1.0f : -1.0f;
        return static_cast<float>(player->m_yVelocity) * gravitySign;
    }

    void sendInput(PlayLayer* layer, bool down, bool isPlayer1) {
        if (!layer) return;
        const bool previousInjecting = g_injectingInput;
        g_injectingInput = true;
        layer->handleButton(down, 1, isPlayer1);
        g_injectingInput = previousInjecting;
    }

    void setAutomaticInput(PlayLayer* layer, PlayerState& state, bool down, bool isPlayer1) {
        if (!layer || state.autoPressed == down) return;
        sendInput(layer, down, isPlayer1);
        // The synthetic handleButton hook also updates this state. Keep the
        // assignment here as a safe fallback if another hook short-circuits.
        state.autoPressed = down;
    }

    void releaseAutomaticInput(PlayLayer* layer, PlayerState& state, bool isPlayer1) {
        if (layer && state.autoPressed) sendInput(layer, false, isPlayer1);
        state.autoPressed = false;
        state.toggleTimer = 0.0f;
    }

    void relinquishAssist(PlayLayer* layer, PlayerState& state, bool isPlayer1, bool restoreManualHold) {
        const bool shouldRestoreHold = restoreManualHold && state.manualHeld;
        releaseAutomaticInput(layer, state, isPlayer1);
        state.assistActive = false;
        state.activityTimer = 0.0f;
        state.manualCps = 0.0f;
        state.qualifyingIntervals = 0;
        state.lastManualPressAt = -1.0f;

        // If assist is disabled or the player changes mode while the physical
        // key is still down, return that hold to the native game input.
        if (shouldRestoreHold && layer) sendInput(layer, true, isPlayer1);
        if (!restoreManualHold) state.manualHeld = false;
    }

    void resetPlayerStates() {
        g_players = {};
    }

    void releaseAll(PlayLayer* layer) {
        releaseAutomaticInput(layer, g_players[0], true);
        releaseAutomaticInput(layer, g_players[1], false);
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
        config.maxCps = static_cast<int>(mod->getSettingValue<int64_t>("max-cps"));
        return config;
    }

    // Count physical press-to-press intervals. Two consecutive qualifying
    // intervals arm assist; below threshold every input remains vanilla.
    void noteManualInput(PlayerState& state, bool down) {
        if (down == state.manualHeld) return;

        if (down) {
            if (state.lastManualPressAt >= 0.0f) {
                const float interval = state.localClock - state.lastManualPressAt;
                if (interval >= 0.008f && interval <= 1.0f) {
                    const float measured = std::clamp(1.0f / interval, 0.0f, 120.0f);
                    state.manualCps = state.manualCps <= 0.0f
                        ? measured
                        : state.manualCps * 0.35f + measured * 0.65f;
                    if (measured >= triggerCps() * 0.98f) {
                        ++state.qualifyingIntervals;
                    } else {
                        state.qualifyingIntervals = 0;
                    }
                } else {
                    state.qualifyingIntervals = 0;
                    if (interval > 1.0f) state.manualCps = 0.0f;
                }
            }
            state.lastManualPressAt = state.localClock;
            state.manualHeld = true;
        } else {
            state.manualHeld = false;
        }

        if (state.assistActive) state.activityTimer = safeActivityWindow();
    }

    void activateAssist(PlayerState& state) {
        state.assistActive = true;
        state.activityTimer = safeActivityWindow();
        state.toggleTimer = 0.0f;
        state.autoPressed = false;
        // Do not reset manualHeld: the threshold-crossing physical press is
        // consumed and now merely keeps the assist armed until its release.
    }

    void runLegacyStraightController(PlayerObject* player, PlayerState& state,
                                     bool selected, bool isPlayer1, float dt) {
        if (!player) return;

        auto* layer = g_activeLayer;
        const float safeDt = std::clamp(dt, 0.0f, 0.05f);
        state.localClock += safeDt;
        state.activityTimer = std::max(0.0f, state.activityTimer - safeDt);
        state.toggleTimer = std::max(0.0f, state.toggleTimer - safeDt);
        state.telemetryTimer = std::max(0.0f, state.telemetryTimer - safeDt);
        ++state.playerUpdateCalls;

        if (player->m_isDead) {
            if (state.assistActive) relinquishAssist(layer, state, isPlayer1, false);
            state.manualHeld = false;
            state.lastManualPressAt = -1.0f;
            state.manualCps = 0.0f;
            state.qualifyingIntervals = 0;
            return;
        }

        if (!g_config.enabled || !selected || !player->m_isShip) {
            if (state.assistActive) relinquishAssist(layer, state, isPlayer1, true);
            return;
        }

        if (!state.assistActive) return;

        // While the physical key is held, assist remains active. After release,
        // the activity window keeps it armed for the next spam burst.
        if (!state.manualHeld && state.activityTimer <= 0.0f) {
            relinquishAssist(layer, state, isPlayer1, false);
            return;
        }

        // Original v0.1.0 hysteresis behavior: press when the Ship is moving
        // with gravity; release when it is moving against gravity. Unlike the
        // later fixed-CPS oscillator, this reacts to actual vertical velocity.
        const float error = gravityRelativeVelocity(player) - g_config.targetVelocity;
        const float deadzone = deadzoneForStraightness();

        bool desiredPressed = state.autoPressed;
        if (error > deadzone) {
            desiredPressed = true;
        } else if (error < -deadzone) {
            desiredPressed = false;
        }

        if (desiredPressed != state.autoPressed && state.toggleTimer <= 0.0f) {
            setAutomaticInput(layer, state, desiredPressed, isPlayer1);
            // max-cps limits complete press/release cycles per second, just as
            // in v0.1.0. The controller is not phase-locked to manual clicks.
            state.toggleTimer = 1.0f / (2.0f * static_cast<float>(maxCps()));
        }

        if (g_config.debugTelemetry && state.telemetryTimer <= 0.0f) {
            log::info(
                "AA {}: mode=LEGACY_HYSTERESIS, manualCPS={:.2f}, trigger={:.2f}, maxCPS={}, straightness={:.1f}, deadzone={:.3f}, gravityVy={:.4f}, autoPressed={}, manualHeld={}, active={}, updates={}, events={}",
                isPlayer1 ? "P1" : "P2",
                state.manualCps,
                triggerCps(),
                maxCps(),
                g_config.straightness,
                deadzone,
                gravityRelativeVelocity(player),
                state.autoPressed,
                state.manualHeld,
                state.assistActive,
                state.playerUpdateCalls,
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
        alternate_assist::releaseAll(this);
        alternate_assist::resetPlayerStates();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        alternate_assist::releaseAll(this);
        alternate_assist::resetPlayerStates();
        alternate_assist::g_activeLayer = nullptr;
        PlayLayer::onQuit();
    }
};

class $modify(AlternateAssistBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto* layer = alternate_assist::g_activeLayer;
        const bool isActivePlayLayer = layer &&
            static_cast<GJBaseGameLayer*>(layer) == this;

        if (!isActivePlayLayer || button != 1) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        // Synthetic controller edges must always reach the original handler.
        if (alternate_assist::g_injectingInput) {
            auto& state = alternate_assist::g_players[isPlayer1 ? 0u : 1u];
            state.autoPressed = down;
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        // In single-player, Up Arrow may be mapped by the user's keybinds to
        // P2 jump. Route that specific P2 stream to P1 when the option is on.
        const bool remapP2ToP1 = !isPlayer1 &&
            alternate_assist::g_config.routePlayer2JumpToPlayer1 &&
            !layer->m_gameState.m_isDualMode;
        const bool effectivePlayer1 = isPlayer1 || remapP2ToP1;
        auto& state = alternate_assist::g_players[effectivePlayer1 ? 0u : 1u];
        auto* player = effectivePlayer1 ? layer->m_player1 : layer->m_player2;
        const bool selected = effectivePlayer1 || alternate_assist::g_config.player2Enabled;
        const bool shipCandidate = alternate_assist::g_config.enabled && selected &&
            player && player->m_isShip && !player->m_isDead;

        if (!shipCandidate) {
            if (state.assistActive) {
                alternate_assist::relinquishAssist(layer, state, effectivePlayer1, false);
            }
            state.manualHeld = down;
            GJBaseGameLayer::handleButton(down, button, effectivePlayer1);
            return;
        }

        alternate_assist::noteManualInput(state, down);
        ++state.manualInputEvents;

        if (state.assistActive) {
            // Physical input is now only the activation/keep-alive signal. It
            // does not determine the press/release timing of the controller.
            state.activityTimer = alternate_assist::safeActivityWindow();
            return;
        }

        if (down && state.qualifyingIntervals >= 2 &&
            state.manualCps >= alternate_assist::triggerCps() * 0.98f) {
            // Consume the threshold-crossing press. The previous physical
            // release was already sent, so this does not leave a held input.
            alternate_assist::activateAssist(state);
            if (alternate_assist::g_config.debugTelemetry) {
                log::info("AA P{} activated: manualCPS={:.2f}, trigger={:.2f}",
                    effectivePlayer1 ? 1 : 2,
                    state.manualCps,
                    alternate_assist::triggerCps()
                );
            }
            return;
        }

        // Before the CPS threshold, preserve native behavior and let all other
        // mods (including input buffers) observe the original press/release.
        GJBaseGameLayer::handleButton(down, button, effectivePlayer1);
    }
};

class $modify(AlternateAssistPlayerObject, PlayerObject) {
    // Deliberately use the same regular PlayerObject::update entry point as
    // the first working prototype, rather than adding another updateJump
    // physics hook that could compete with Nova's physics-hook detection.
    void update(float dt) {
        PlayerObject::update(dt);

        auto* layer = alternate_assist::g_activeLayer;
        if (!layer) return;

        if (this == layer->m_player1) {
            alternate_assist::runLegacyStraightController(
                this, alternate_assist::g_players[0], true, true, dt
            );
        } else if (this == layer->m_player2) {
            alternate_assist::runLegacyStraightController(
                this,
                alternate_assist::g_players[1],
                alternate_assist::g_config.player2Enabled,
                false,
                dt
            );
        }
    }
};

$on_mod(Loaded) {
    alternate_assist::g_config = alternate_assist::readConfig();

    listenForSettingChanges<bool>("assist-enabled", [](bool value) {
        alternate_assist::g_config.enabled = value;
        if (!value && alternate_assist::g_activeLayer) {
            for (std::size_t i = 0; i < alternate_assist::g_players.size(); ++i) {
                auto& state = alternate_assist::g_players[i];
                if (state.assistActive) {
                    alternate_assist::relinquishAssist(
                        alternate_assist::g_activeLayer, state, i == 0, true
                    );
                }
            }
        }
    });

    listenForSettingChanges<bool>("player2-enabled", [](bool value) {
        alternate_assist::g_config.player2Enabled = value;
        auto& state = alternate_assist::g_players[1];
        if (!value && alternate_assist::g_activeLayer && state.assistActive) {
            alternate_assist::relinquishAssist(
                alternate_assist::g_activeLayer, state, false, true
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

    listenForSettingChanges<double>("target-vertical-velocity", [](double value) {
        alternate_assist::g_config.targetVelocity = static_cast<float>(value);
    });

    listenForSettingChanges<double>("manual-activity-window", [](double value) {
        alternate_assist::g_config.manualActivityWindow = static_cast<float>(value);
    });

    listenForSettingChanges<double>("trigger-cps", [](double value) {
        alternate_assist::g_config.triggerCps = static_cast<float>(value);
    });

    listenForSettingChanges<int64_t>("max-cps", [](int64_t value) {
        alternate_assist::g_config.maxCps = static_cast<int>(value);
    });

    log::info("Alternate Assist Mod v0.1.8 loaded. Legacy velocity-hysteresis controller is gated by a manual CPS threshold.");
}
