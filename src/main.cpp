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
        float resumeDelay = 0.12f;
        int maxCps = 30;
    };

    struct PlayerState {
        bool autoPressed = false;
        bool manualHeld = false;
        float resumeTimer = 0.0f;
        float toggleTimer = 0.0f;
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

    void sendInput(PlayLayer* layer, bool down, bool isPlayer1) {
        if (!layer) return;
        const bool wasInjecting = g_injectingInput;
        g_injectingInput = true;
        layer->handleButton(down, 1, isPlayer1);
        g_injectingInput = wasInjecting;
    }

    void setAutomaticInput(PlayLayer* layer, PlayerState& state, bool down, bool isPlayer1) {
        if (!layer || state.autoPressed == down) return;
        sendInput(layer, down, isPlayer1);
        state.autoPressed = down;
    }

    void releaseAutomaticInput(PlayLayer* layer, PlayerState& state, bool isPlayer1) {
        if (layer && state.autoPressed) {
            sendInput(layer, false, isPlayer1);
        }
        state.autoPressed = false;
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
        config.maxCps = static_cast<int>(mod->getSettingValue<int64_t>("max-cps"));
        return config;
    }

    void runAssist(PlayerObject* player, PlayerState& state, bool selected, bool isPlayer1, float dt) {
        if (!player) return;

        const float safeDt = std::clamp(dt, 0.0f, 0.05f);
        state.resumeTimer = std::max(0.0f, state.resumeTimer - safeDt);
        state.toggleTimer = std::max(0.0f, state.toggleTimer - safeDt);

        // Clear stale manual-hold state on death so respawns cannot leave the
        // controller permanently suspended if the game swallows the key-up.
        if (player->m_isDead) {
            releaseAutomaticInput(g_activeLayer, state, isPlayer1);
            state.manualHeld = false;
            state.resumeTimer = 0.15f;
            return;
        }

        // Never keep an injected hold through a mode change or disable.
        if (!g_config.enabled || !selected || !player->m_isShip) {
            releaseAutomaticInput(g_activeLayer, state, isPlayer1);
            return;
        }

        if (state.manualHeld) {
            releaseAutomaticInput(g_activeLayer, state, isPlayer1);
            return;
        }

        if (state.resumeTimer > 0.0f) return;

        // The ship's thrust opposes gravity. Convert world Y velocity into a
        // gravity-relative axis so normal and inverted gravity use the same rule.
        const float gravityDirection = player->m_isUpsideDown ? 1.0f : -1.0f;
        const float gravityRelativeVelocity = player->m_yVelocity * gravityDirection;
        const float error = gravityRelativeVelocity - g_config.targetVelocity;
        const float deadzone = deadzoneForProfile();

        // Hysteresis controller: press while drifting with gravity; release
        // while moving against gravity. Inside the band, keep the last state.
        bool desiredPressed = state.autoPressed;
        if (error > deadzone) {
            desiredPressed = true;
        } else if (error < -deadzone) {
            desiredPressed = false;
        }

        if (desiredPressed == state.autoPressed || state.toggleTimer > 0.0f) return;

        setAutomaticInput(g_activeLayer, state, desiredPressed, isPlayer1);

        // max-cps is complete press/release cycles per second, so each edge is
        // rate-limited to half that frequency. The 240 Hz game update can honor
        // up to 60 full cycles/s without frame-based busy loops.
        state.toggleTimer = 1.0f / (2.0f * static_cast<float>(maxCps()));
    }

    void noteManualInput(bool down, bool isPlayer1) {
        const std::size_t index = isPlayer1 ? 0u : 1u;
        auto& state = g_players[index];

        if (down) {
            state.manualHeld = true;
            state.resumeTimer = 0.0f;
            // The physical input now owns the held state. Do not send a
            // compensating release here; that would cancel the player's click.
            state.autoPressed = false;
        } else {
            state.manualHeld = false;
            state.autoPressed = false;
            state.resumeTimer = std::clamp(g_config.resumeDelay, 0.0f, 1.0f);
        }
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
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        // Button 1 is Geometry Dash's primary jump/Ship input. Record only
        // real gameplay input routed through the game layer; automatic pulses
        // use the same route but are marked by g_injectingInput.
        if (button == 1 && !alternate_assist::g_injectingInput &&
            alternate_assist::g_activeLayer &&
            static_cast<GJBaseGameLayer*>(alternate_assist::g_activeLayer) == this) {
            alternate_assist::noteManualInput(down, isPlayer1);
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

    listenForSettingChanges<int64_t>("max-cps", [](int64_t value) {
        alternate_assist::g_config.maxCps = static_cast<int>(value);
    });

    log::info("Alternate Assist Mod v0.1.0 loaded. Assistance is disabled by default.");
}
