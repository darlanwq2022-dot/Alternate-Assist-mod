#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <cmath>
#include <algorithm>

using namespace geode::prelude;

// Passos de física por segundo (GD 2.2 roda a 240 TPS por padrão)
static constexpr float TPS = 240.f;

static std::string assistLabel() {
	auto mod = Mod::get();
	if (!mod->getSettingValue<bool>("enabled")) return "Assist: OFF";
	return "Assist: " + mod->getSettingValue<std::string>("mode");
}

// Ciclo: Extremo -> Semi-reto -> Pouca ajuda -> Custom -> OFF -> Extremo
static void cycleAssist() {
	auto mod = Mod::get();
	if (!mod->getSettingValue<bool>("enabled")) {
		mod->setSettingValue<bool>("enabled", true);
		mod->setSettingValue<std::string>("mode", "Extremo");
		return;
	}
	auto m = mod->getSettingValue<std::string>("mode");
	if (m == "Extremo") mod->setSettingValue<std::string>("mode", "Semi-reto");
	else if (m == "Semi-reto") mod->setSettingValue<std::string>("mode", "Pouca ajuda");
	else if (m == "Pouca ajuda") mod->setSettingValue<std::string>("mode", "Custom");
	else mod->setSettingValue<bool>("enabled", false);
}

class $modify(AssistGameLayer, GJBaseGameLayer) {
	struct Fields {
		bool userHolding = false;   // jogador está segurando de verdade
		bool simDown = false;       // estado do clique simulado
		bool injecting = false;     // evita recursão
		int stepCounter = 0;
	};

	bool assistActive() {
		if (!Mod::get()->getSettingValue<bool>("enabled")) return false;
		if (Mod::get()->getSettingValue<bool>("practice-only") && !m_isPracticeMode) return false;
		if (!m_player1 || !m_player1->m_isShip) return false;
		return true;
	}

	void inject(bool down) {
		auto f = m_fields.operator->();
		if (f->simDown == down) return;
		f->simDown = down;
		f->injecting = true;
		GJBaseGameLayer::handleButton(down, 1, true);
		f->injecting = false;
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		auto f = m_fields.operator->();

		if (f->injecting || button != 1 || !isPlayer1) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		// input real do jogador
		f->userHolding = down;

		if (assistActive()) {
			// o assist controla o clique enquanto o jogador segura
			if (!down) inject(false);
			return;
		}

		f->simDown = down;
		GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}

	void processCommands(float dt) {
		GJBaseGameLayer::processCommands(dt);

		auto f = m_fields.operator->();

		// saiu do modo ship / assist desligado: devolve o controle ao jogador
		if (!assistActive()) {
			if (f->simDown != f->userHolding && !f->injecting) {
				inject(f->userHolding);
			}
			return;
		}

		if (!f->userHolding) {
			inject(false);
			return;
		}

		f->stepCounter++;
		auto mode = Mod::get()->getSettingValue<std::string>("mode");
		float velY = m_player1->m_yVelocity;

		if (mode == "Extremo") {
			// feedback a cada passo: mantém a velocidade vertical ~0
			inject(velY < 0.f);
		}
		else if (mode == "Semi-reto") {
			// feedback com zona morta + limite de ~30 CPS
			int minGap = std::max(1, (int)std::round(TPS / 60.f));
			if (f->stepCounter >= minGap) {
				if (velY < -0.5f) inject(true);
				else if (velY > 0.5f) inject(false);
				f->stepCounter = 0;
			}
		}
		else {
			// Pouca ajuda / Custom: pulsos cegos em CPS fixo
			int cps = (mode == "Pouca ajuda")
				? 8
				: (int)Mod::get()->getSettingValue<int64_t>("custom-cps");
			int half = std::max(1, (int)std::round(TPS / (2.f * cps)));
			if (f->stepCounter >= half) {
				inject(!f->simDown);
				f->stepCounter = 0;
			}
		}
	}
};


// Botão no menu de pausa
class $modify(AssistPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto winSize = CCDirector::get()->getWinSize();
		auto label = CCLabelBMFont::create(assistLabel().c_str(), "bigFont.fnt");
		label->setScale(0.5f);

		auto btn = CCMenuItemExt::createSpriteExtra(label, [label](CCObject*) {
			cycleAssist();
			label->setString(assistLabel().c_str());
		});

		auto menu = CCMenu::create();
		menu->setPosition({winSize.width - 110.f, 25.f});
		menu->addChild(btn);
		this->addChild(menu);
	}
};
