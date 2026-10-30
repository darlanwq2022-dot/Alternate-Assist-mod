#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

using namespace geode::prelude;

namespace alternate_assist {
    struct Config {
        bool enabled = false;
        bool player2Enabled = false;
        std::string profile = "Extreme Straight";
        float customDeadzone = 0.20f;
        float targetVelocity = 0.0f;
        float resumeDelay = 0.02f;
        float manualActivityWindow = 0.24f;
        float inputAlignment = 0.75f;
        int maxCps = 30;
    };

    struct PlayerState {
        // The input sent to the game is the OR of manualHeld and autoPressed.
        // This lets manual clicks and assistance pulses share one logical
        // button without cancelling or double-pressing each other.
        bool autoPressed = false;
        bool manualHeld = false;
        float resumeTimer = 0.0f;
        float toggleTimer = 0.0f;
        float manualActivityTimer = 0.0f;
        float localClock = 0.0f;
        float lastManualPressAt = -1.0f;
        float manualCadenceHz = 0.0f;
    };

    Config g_config;
    std::array<PlayerState, 2> g_players{};
    PlayLayer* g_activeLayer = nullptr;
    bool g_injectingInput = false;

    float deadzoneForProfile() {
        if (g_config.profile == "Extreme Straight") return 0.18f;
        if (g_config.profile == "Semi-Straight") return 0.62f;
        if (g_config.profile == "Light Assist") return 1.35f;
        return std::clamp(g_config.customDeadzone, 0.05f, 3.0f);
    }

    int maxCps() {
        return std::clamp(g_config.maxCps, 1, 60);
    }

    float activityWindow() {
        return std::clamp(g_config.manualActivityWindow, 0.05f, 1.0f);
    }

    bool effectiveButtonState(PlayerState const& state) {
        return state.manualHeld || state.autoPressed;
    }

    void sendAutomaticInput(PlayLayer* layer, bool down, bool isPlayer1) {
        if (!layer) return;
        const bool wasInjecting = g_injectingInput;
        g_injectingInput = true;
        layer->handleButton(down, 1, isPlayer1);
        g_injectingInput = wasInjecting;
    }

    void setAutomaticInput(PlayLayer* layer, PlayerState& state, bool down, bool isPlayer1) {
        if (!layer || state.autoPressed == down) return;
        sendAutomaticInput(layer, down, isPlayer1);
    }

    void releaseAutomaticInput(PlayLayer* layer, PlayerState& state, bool isPlayer1) {
        if (layer && state.autoPressed) {
            sendAutomaticInput(layer, false, isPlayer1);
        }
        state.toggleTimer = 0.0f;
    }

    void releaseAllAutomaticInputs(PlayLayer* layer) {
        releaseAutomaticInput(layer, g_players[0], true);
        releaseAutomaticInput(layer, g_players[1], false);
    }

    void resetPlayerStates() {
        g_players = {};
    }

    Config readConfig() {
        auto* mod = Mod::get();
        Config config;
        config.enabled = mod->getSettingValue<bool>("assist-enabled");
        config.player2Enabled = mod->getSettingValue<bool>("player2-enabled");
        config.profile = mod->getSettingValue<std::string>("assist-profile");
        config.customDeadzone = static_cast<float>(mod->getSettingValue<double>("custom-deadzone"));
        config.targetVelocity = static_cast<float>(mod->getSettingValue<double>("target-vertical-velocity"));
        config.resumeDelay = static_cast<float>(mod->getSettingValue<double>("manual-resume-delay"));
        config.manualActivityWindow = static_cast<float>(mod->getSettingValue<double>("manual-activity-window"));
        config.inputAlignment = static_cast<float>(mod->getSettingValue<double>("input-alignment"));
        config.maxCps = static_cast<int>(mod->getSettingValue<int64_t>("max-cps"));
        return config;
    }

    void noteManualInput(PlayerState& state, bool down) {
        if (down) {
            // Estimate the user's physical click cadence. It is used as an
            // additional feedback signal; manual input is never replayed.
            if (state.lastManualPressAt >= 0.0f) {
                const float interval = state.localClock - state.lastManualPressAt;
                if (interval >= 0.008f && interval <= 1.0f) {
                    const float measuredHz = std::clamp(1.0f / interval, 1.0f, 60.0f);
                    if (state.manualCadenceHz <= 0.0f) {
                        state.manualCadenceHz = measuredHz;
                    } else {
                        // Smooth noisy human input instead of chasing each edge.
                        state.manualCadenceHz =
                            state.manualCadenceHz * 0.65f + measuredHz * 0.35f;
                    }
                } else if (interval > 1.0f) {
                    state.manualCadenceHz = 0.0f;
                }
            }
            state.lastManualPressAt = state.localClock;
            state.manualActivityTimer = activityWindow();
            state.resumeTimer = 0.0f;

            // The physical press takes ownership. If an automatic pulse was
            // down, ownership transfers without sending a duplicate down/up.
            state.manualHeld = true;
            state.autoPressed = false;
        } else {
            state.manualHeld = false;
            // Keep assistance armed only for a short period after the latest
            // real click. Repeated clicking refreshes this timer; idle does not.
            state.manualActivityTimer = std::max(
                state.manualActivityTimer,
                activityWindow()
            );
            state.resumeTimer = std::clamp(g_config.resumeDelay, 0.0f, 1.0f);
        }
    }

    void runAssist(PlayerObject* player, PlayerState& state, bool selected, bool isPlayer1, float dt) {
        if (!player) return;

        const float safeDt = std::clamp(dt, 0.0f, 0.05f);
        state.localClock += safeDt;
        state.resumeTimer = std::max(0.0f, state.resumeTimer - safeDt);
        state.toggleTimer = std::max(0.0f, state.toggleTimer - safeDt);
        state.manualActivityTimer = std::max(0.0f, state.manualActivityTimer - safeDt);

        if (player->m_isDead) {
            releaseAutomaticInput(g_activeLayer, state, isPlayer1);
            state.manualHeld = false;
            state.manualActivityTimer = 0.0f;
            state.manualCadenceHz = 0.0f;
            state.lastManualPressAt = -1.0f;
            state.resumeTimer = 0.15f;
            return;
        }

        // Never run in the background: the user must have clicked recently.
        if (!g_config.enabled || !selected || !player->m_isShip) {
            releaseAutomaticInput(g_activeLayer, state, isPlayer1);
            state.manualActivityTimer = 0.0f;
            state.manualCadenceHz = 0.0f;
            return;
        }

        // While the physical button is down, it owns the input. Automatic
        // pulses fill only the gaps between real clicks.
        if (state.manualHeld) {
            releaseAutomaticInput(g_activeLayer, state, isPlayer1);
            return;
        }

        // A single click briefly arms assistance. Continued clicking refreshes
        // the window; when the user stops, assistance releases and goes idle.
        if (state.manualActivityTimer <= 0.0f) {
            releaseAutomaticInput(g_activeLayer, state, isPlayer1);
            state.manualCadenceHz = 0.0f;
            return;
        }

        if (state.resumeTimer > 0.0f) return;

        // Gravity-relative velocity makes the rule work for normal/inverted
        // gravity. The manual cadence tightens the tolerance progressively:
        // faster real clicks make the controller more precise, not more active
        // while the user is idle.
        const float gravityDirection = player->m_isUpsideDown ? 1.0f : -1.0f;
        const float gravityRelativeVelocity = player->m_yVelocity * gravityDirection;
        const float error = gravityRelativeVelocity - g_config.targetVelocity;
        const float baseDeadzone = deadzoneForProfile();
        const float cadenceFactor = std::clamp(state.manualCadenceHz / 18.0f, 0.0f, 1.0f);
        const float alignment = std::clamp(g_config.inputAlignment, 0.0f, 1.0f);
        const float deadzone = std::max(
            0.04f,
            baseDeadzone * (1.0f - 0.30f * alignment * cadenceFactor)
        );

        // Hysteresis avoids rapid oscillation. Manual clicks and synthetic
        // pulses are arbitrated as one button state in handleButton below.
        bool desiredPressed = state.autoPressed;
        if (error > deadzone) {
            desiredPressed = true;
        } else if (error < -deadzone) {
            desiredPressed = false;
        }

        if (desiredPressed == state.autoPressed || state.toggleTimer > 0.0f) return;

        setAutomaticInput(g_activeLayer, state, desiredPressed, isPlayer1);
        state.toggleTimer = 1.0f / (2.0f * static_cast<float>(maxCps()));
    }
}

class $modify(AlternateAssistPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        alternate_assist::g_activeLayer = nullptr;
        alternate_assist::resetPlayerStates();
        const bool result = PlayLayer::init(level, useReplay, dontCreateObjects);
        if (result) {
            alternate_assist::g_activeLayer = this;
        }
        return result;
    }

    void resetLevel() {
        alternate_assist::releaseAllAutomaticInputs(this);
        alternate_assist::resetPlayerStates();
        PlayLayer::resetLevel();
    }

    void onQuit() {
        alternate_assist::releaseAllAutomaticInputs(this);
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
        const bool wasEffective = alternate_assist::effectiveButtonState(state);

        if (alternate_assist::g_injectingInput) {
            state.autoPressed = down;
        } else {
            alternate_assist::noteManualInput(state, down);
        }

        const bool isEffective = alternate_assist::effectiveButtonState(state);
        // Forward only effective state transitions. A manual press can take
        // over an automatic hold, and an automatic release cannot cancel a
        // physical hold. This is the input-alignment/ownership guarantee.
        if (wasEffective != isEffective) {
            GJBaseGameLayer::handleButton(isEffective, button, isPlayer1);
        }
    }
};

class $modify(AlternateAssistPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);

        auto* layer = alternate_assist::g_activeLayer;
        if (!layer) return;

        if (this == layer->m_player1) {
            alternate_assist::runAssist(
                this,
                alternate_assist::g_players[0],
                true,
                true,
                dt
            );
        } else if (this == layer->m_player2) {
            alternate_assist::runAssist(
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
        const bool wasEnabled = alternate_assist::g_config.enabled;
        alternate_assist::g_config.enabled = value;
        if (wasEnabled && !value) {
            alternate_assist::releaseAllAutomaticInputs(alternate_assist::g_activeLayer);
            alternate_assist::g_players[0].manualActivityTimer = 0.0f;
            alternate_assist::g_players[1].manualActivityTimer = 0.0f;
        }
    });

    listenForSettingChanges<bool>("player2-enabled", [](bool value) {
        alternate_assist::g_config.player2Enabled = value;
        if (!value && alternate_assist::g_activeLayer) {
            alternate_assist::releaseAutomaticInput(
                alternate_assist::g_activeLayer,
                alternate_assist::g_players[1],
                false
            );
            alternate_assist::g_players[1].manualActivityTimer = 0.0f;
        }
    });

    listenForSettingChanges<std::string>("assist-profile", [](std::string value) {
        alternate_assist::g_config.profile = std::move(value);
    });

    listenForSettingChanges<double>("custom-deadzone", [](double value) {
        alternate_assist::g_config.customDeadzone = static_cast<float>(value);
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

    listenForSettingChanges<int64_t>("max-cps", [](int64_t value) {
        alternate_assist::g_config.maxCps = static_cast<int>(value);
    });

    log::info("Alternate Assist Mod v0.1.2 loaded. Assist is gated by recent manual input.");
}
