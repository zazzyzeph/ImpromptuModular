//***********************************************************************************************
//Six channel 64-step sequencer module for VCV Rack by Marc Boulé
//
//Based on code from the Fundamental and AudibleInstruments plugins by Andrew Belt 
//and graphics from the Component Library by Wes Milholen 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//Based on the BigButton sequencer by Look-Mum-No-Computer
//https://www.youtube.com/watch?v=6ArDGcUqiWM
//https://www.lookmumnocomputer.com/projects/#/big-button/
//
//***********************************************************************************************


#include "ImpromptuModular.hpp"
#include "PhraseSeqUtil.hpp"


struct BigButtonTest : Module {
	enum ParamIds {
		CHAN_PARAM,
		LEN_PARAM,
		RND_PARAM,
		RESET_PARAM,
		CLEAR_PARAM,
		BANK_PARAM,
		DEL_PARAM,
		FILL_PARAM,
		BIG_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLK_INPUT,
		CHAN_INPUT,
		BIG_INPUT,
		LEN_INPUT,
		RND_INPUT,
		RESET_INPUT,
		CLEAR_INPUT,
		BANK_INPUT,
		DEL_INPUT,
		FILL_INPUT,
		// -- 0.6.10 ^^
		CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(CHAN_OUTPUTS, 6),
		// -- 0.6.10 ^^
		ENUMS(CV_OUTPUTS, 6),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(CHAN_LIGHTS, 6 * 2),// Room for GreenRed
		BIG_LIGHT,
		BIGC_LIGHT,
		ENUMS(METRONOME_LIGHT, 2),// Room for GreenRed
		NUM_LIGHTS
	};
	
	// Need to save
	int panelTheme = 0;
	int metronomeDiv = 4;
	bool writeFillsToMemory = false;
	bool quantizeBig;
	int indexStep;
	int bank[6];
	uint64_t gates[6][2];// chan , bank
	float cv[6][2][64];// chan , bank , step
	
	// No need to save
	long clockIgnoreOnReset;
	double lastPeriod;//2.0 when not seen yet (init or stopped clock and went greater than 2s, which is max period supported for time-snap)
	double clockTime;//clock time counter (time since last clock)
	int pendingOp;// 0 means nothing pending, +1 means pending big button push, -1 means pending del
	float pendingCV;// 


	unsigned int lightRefreshCounter = 0;	
	float bigLight = 0.0f;
	float metronomeLightStart = 0.0f;
	float metronomeLightDiv = 0.0f;
	int chan = 0;
	int len = 0; 
	SchmittTrigger clockTrigger;
	SchmittTrigger resetTrigger;
	SchmittTrigger bankTrigger;
	SchmittTrigger bigTrigger;
	PulseGenerator outPulse;
	PulseGenerator outLightPulse;
	PulseGenerator bigPulse;
	PulseGenerator bigLightPulse;

	
	inline void toggleGate(int chan) {gates[chan][bank[chan]] ^= (((uint64_t)1) << (uint64_t)indexStep);}
	inline void setGate(int chan) {gates[chan][bank[chan]] |= (((uint64_t)1) << (uint64_t)indexStep);}
	inline void setCV(int chan, float cvValue) {cv[chan][bank[chan]][indexStep] = cvValue;}
	inline void clearGate(int chan) {gates[chan][bank[chan]] &= ~(((uint64_t)1) << (uint64_t)indexStep);}
	inline bool getGate(int chan) {return !((gates[chan][bank[chan]] & (((uint64_t)1) << (uint64_t)indexStep)) == 0);}

	
	BigButtonTest() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {		
		onReset();
	}

	
	void onReset() override {
		quantizeBig = true;
		indexStep = 0;
		for (int c = 0; c < 6; c++) {
			bank[c] = 0;
			for (int b = 0; b < 2; b++) {
				gates[c][b] = 0;
				for (int s = 0; s < 64; s++)
					cv[c][b][s] = 0.0f;
			}
		}
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * engineGetSampleRate());
		lastPeriod = 2.0;
		clockTime = 0.0;
		pendingOp = 0;
		pendingCV = 0.0f;
	}


	void onRandomize() override {
		indexStep = randomu32() % 32;
		for (int c = 0; c < 6; c++) {
			bank[c] = randomu32() % 2;
			for (int b = 0; b < 2; b++) {
				gates[c][b] = randomu64();
				for (int s = 0; s < 64; s++)
					cv[c][b][s] = ((float)(randomu32() % 7)) + ((float)(randomu32() % 12)) / 12.0f - 3.0f;
			}
		}
		//clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * engineGetSampleRate());
		//lastPeriod = 2.0;
		//clockTime = 0.0;
		//pendingOp = 0;
		//pendingCV = 0.0f;
	}

	
	json_t *toJson() override {
		json_t *rootJ = json_object();

		// indexStep
		json_object_set_new(rootJ, "indexStep", json_integer(indexStep));

		// bank
		json_t *bankJ = json_array();
		for (int c = 0; c < 6; c++)
			json_array_insert_new(bankJ, c, json_integer(bank[c]));
		json_object_set_new(rootJ, "bank", bankJ);

		// gates
		json_t *gatesJ = json_array();
		for (int c = 0; c < 6; c++)
			for (int b = 0; b < 8; b++) {// bank to store is like to uint64_t to store, so go to 8
				// first to get stored is 16 lsbits of bank 0, then next 16 bits,... to 16 msbits of bank 1
				unsigned int intValue = (unsigned int) ( (uint64_t)0xFFFF & (gates[c][b/4] >> (uint64_t)(16 * (b % 4))) );
				json_array_insert_new(gatesJ, b + (c << 3) , json_integer(intValue));
			}
		json_object_set_new(rootJ, "gates", gatesJ);

		// CV bank 0
		json_t *cv0J = json_array();
		for (int c = 0; c < 6; c++) {
			for (int s = 0; s < 64; s++) {
				json_array_insert_new(cv0J, s + c * 64, json_real(cv[c][0][s]));
			}
		}
		json_object_set_new(rootJ, "cv0", cv0J);
		// CV bank 1
		json_t *cv1J = json_array();
		for (int c = 0; c < 6; c++) {
			for (int s = 0; s < 64; s++) {
				json_array_insert_new(cv1J, s + c * 64, json_real(cv[c][1][s]));
			}
		}
		json_object_set_new(rootJ, "cv1", cv1J);

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// metronomeDiv
		json_object_set_new(rootJ, "metronomeDiv", json_integer(metronomeDiv));

		// writeFillsToMemory
		json_object_set_new(rootJ, "writeFillsToMemory", json_boolean(writeFillsToMemory));

		// quantizeBig
		json_object_set_new(rootJ, "quantizeBig", json_boolean(quantizeBig));

		return rootJ;
	}


	void fromJson(json_t *rootJ) override {
		// indexStep
		json_t *indexStepJ = json_object_get(rootJ, "indexStep");
		if (indexStepJ)
			indexStep = json_integer_value(indexStepJ);

		// bank
		json_t *bankJ = json_object_get(rootJ, "bank");
		if (bankJ)
			for (int c = 0; c < 6; c++)
			{
				json_t *bankArrayJ = json_array_get(bankJ, c);
				if (bankArrayJ)
					bank[c] = json_integer_value(bankArrayJ);
			}

		// gates
		json_t *gatesJ = json_object_get(rootJ, "gates");
		uint64_t bank8ints[8] = {0,0,0,0,0,0,0,0};
		if (gatesJ) {
			for (int c = 0; c < 6; c++) {
				for (int b = 0; b < 8; b++) {// bank to store is like to uint64_t to store, so go to 8
					// first to get read is 16 lsbits of bank 0, then next 16 bits,... to 16 msbits of bank 1
					json_t *gateJ = json_array_get(gatesJ, b + (c << 3));
					if (gateJ)
						bank8ints[b] = (uint64_t) json_integer_value(gateJ);
				}
				gates[c][0] = bank8ints[0] | (bank8ints[1] << (uint64_t)16) | (bank8ints[2] << (uint64_t)32) | (bank8ints[3] << (uint64_t)48);
				gates[c][1] = bank8ints[4] | (bank8ints[5] << (uint64_t)16) | (bank8ints[6] << (uint64_t)32) | (bank8ints[7] << (uint64_t)48);
			}
		}
		
		// CV bank 0
		json_t *cv0J = json_object_get(rootJ, "cv0");
		if (cv0J) {
			for (int c = 0; c < 6; c++)
				for (int s = 0; s < 32; s++) {
					json_t *cv0ArrayJ = json_array_get(cv0J, s + c * 64);
					if (cv0ArrayJ)
						cv[c][0][s] = json_number_value(cv0ArrayJ);
				}
		}
		// CV bank 1
		json_t *cv1J = json_object_get(rootJ, "cv1");
		if (cv1J) {
			for (int c = 0; c < 6; c++)
				for (int s = 0; s < 32; s++) {
					json_t *cv1ArrayJ = json_array_get(cv1J, s + c * 64);
					if (cv1ArrayJ)
						cv[c][1][s] = json_number_value(cv1ArrayJ);
				}
		}
		
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);

		// metronomeDiv
		json_t *metronomeDivJ = json_object_get(rootJ, "metronomeDiv");
		if (metronomeDivJ)
			metronomeDiv = json_integer_value(metronomeDivJ);

		// writeFillsToMemory
		json_t *writeFillsToMemoryJ = json_object_get(rootJ, "writeFillsToMemory");
		if (writeFillsToMemoryJ)
			writeFillsToMemory = json_is_true(writeFillsToMemoryJ);
		
		// quantizeBig
		json_t *quantizeBigJ = json_object_get(rootJ, "quantizeBig");
		if (quantizeBigJ)
			quantizeBig = json_is_true(quantizeBigJ);
	}

	
	void step() override {
		double sampleTime = 1.0 / engineGetSampleRate();
		static const float lightTime = 0.1f;
		
		
		//********** Buttons, knobs, switches and inputs **********
		
		// Length
		len = (int) clamp(roundf( params[LEN_PARAM].value + ( inputs[LEN_INPUT].active ? (inputs[LEN_INPUT].value / 10.0f * (64.0f - 1.0f)) : 0.0f ) ), 0.0f, (64.0f - 1.0f)) + 1;	

		// Chan
		float chanInputValue = inputs[CHAN_INPUT].value / 10.0f * (6.0f - 1.0f);
		chan = (int) clamp(roundf(params[CHAN_PARAM].value + chanInputValue), 0.0f, (6.0f - 1.0f));		
		
		// Big button
		if (bigTrigger.process(params[BIG_PARAM].value + inputs[BIG_INPUT].value)) {
			bigLight = 1.0f;
			if (quantizeBig && (clockTime > (lastPeriod / 2.0)) && (clockTime <= (lastPeriod * 1.01))) {// allow for 1% clock jitter
				pendingOp = 1;
				pendingCV = inputs[CV_INPUT].value;
			}
			else {
				if (!getGate(chan)) {
					setGate(chan);// bank and indexStep are global
					bigPulse.trigger(0.001f);
				}
				setCV(chan, inputs[CV_INPUT].value);
				bigLightPulse.trigger(lightTime);
			}
		}

		// Bank button
		if (bankTrigger.process(params[BANK_PARAM].value + inputs[BANK_INPUT].value))
			bank[chan] = 1 - bank[chan];
		
		// Clear button
		if (params[CLEAR_PARAM].value + inputs[CLEAR_INPUT].value > 0.5f) {
			gates[chan][bank[chan]] = 0;
			for (int s = 0; s < 64; s++)
				cv[chan][bank[chan]][s] = 0.0f;
		}
		
		// Del button
		if (params[DEL_PARAM].value + inputs[DEL_INPUT].value > 0.5f) {
			if (clockTime > (lastPeriod / 2.0) && clockTime <= (lastPeriod * 1.01))// allow for 1% clock jitter
				pendingOp = -1;// overrides the pending write if it exists
			else {
				clearGate(chan);// bank and indexStep are global
				cv[chan][bank[chan]][indexStep] = 0.0f;
			}
		}

		// Fill button
		bool fillPressed = (params[FILL_PARAM].value + inputs[FILL_INPUT].value) > 0.5f;
		if (fillPressed && writeFillsToMemory)
			setGate(chan);// bank and indexStep are global


		// Pending timeout (write/del current step)
		if (pendingOp != 0 && clockTime > (lastPeriod * 1.01) ) 
			performPending(chan, lightTime);

		
		
		//********** Clock and reset **********
		
		// Clock
		if (clockTrigger.process(inputs[CLK_INPUT].value)) {
			if (clockIgnoreOnReset == 0l) {
				outPulse.trigger(0.001f);
				outLightPulse.trigger(lightTime);
				
				indexStep = moveIndex(indexStep, indexStep + 1, len);
				
				if (pendingOp != 0)
					performPending(chan, lightTime);// Proper pending write/del to next step which is now reached
				
				if (indexStep == 0)
					metronomeLightStart = 1.0f;
				metronomeLightDiv = ((indexStep % metronomeDiv) == 0 && indexStep != 0) ? 1.0f : 0.0f;
				
				// Random (toggle gate according to probability knob)
				float rnd01 = params[RND_PARAM].value / 100.0f + inputs[RND_INPUT].value / 10.0f;
				if (rnd01 > 0.0f) {
					if (randomUniform() < rnd01)// randomUniform is [0.0, 1.0), see include/util/common.hpp
						toggleGate(chan);
				}
				lastPeriod = clockTime > 2.0 ? 2.0 : clockTime;
				clockTime = 0.0;
			}
		}
			
		
		// Reset
		if (resetTrigger.process(params[RESET_PARAM].value + inputs[RESET_INPUT].value)) {
			indexStep = 0;
			outPulse.trigger(0.001f);
			outLightPulse.trigger(0.02f);
			metronomeLightStart = 1.0f;
			metronomeLightDiv = 0.0f;
			clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * engineGetSampleRate());
		}		
		
		
		
		//********** Outputs and lights **********
		
		
		// Gate outputs
		bool bigPulseState = bigPulse.process((float)sampleTime);
		bool outPulseState = outPulse.process((float)sampleTime);
		for (int i = 0; i < 6; i++) {
			bool gate = getGate(i);
			bool outSignal = (((gate || (i == chan && fillPressed)) && outPulseState) || (gate && bigPulseState && i == chan));
			outputs[CHAN_OUTPUTS + i].value = outSignal ? 10.0f : 0.0f;
			outputs[CV_OUTPUTS + i].value = cv[i][bank[i]][indexStep];
		}

		
		lightRefreshCounter++;
		if (lightRefreshCounter > displayRefreshStepSkips) {
			lightRefreshCounter = 0;

			// Gate light outputs
			bool bigLightPulseState = bigLightPulse.process((float)sampleTime * displayRefreshStepSkips);
			bool outLightPulseState = outLightPulse.process((float)sampleTime * displayRefreshStepSkips);
			for (int i = 0; i < 6; i++) {
				bool gate = getGate(i);
				bool outLight  = (((gate || (i == chan && fillPressed)) && outLightPulseState) || (gate && bigLightPulseState && i == chan));
				lights[(CHAN_LIGHTS + i) * 2 + 1].setBrightnessSmooth(outLight ? 1.0f : 0.0f, displayRefreshStepSkips);
				lights[(CHAN_LIGHTS + i) * 2 + 0].value = (i == chan ? (1.0f - lights[(CHAN_LIGHTS + i) * 2 + 1].value) / 2.0f : 0.0f);
			}

			// Big button lights
			lights[BIG_LIGHT].value = bank[chan] == 1 ? 1.0f : 0.0f;
			lights[BIGC_LIGHT].value = bigLight;
			
			// Metronome light
			lights[METRONOME_LIGHT + 1].value = metronomeLightStart;
			lights[METRONOME_LIGHT + 0].value = metronomeLightDiv;
		
		
			bigLight -= (bigLight / lightLambda) * (float)sampleTime * displayRefreshStepSkips;	
			metronomeLightStart -= (metronomeLightStart / lightLambda) * (float)sampleTime * displayRefreshStepSkips;	
			metronomeLightDiv -= (metronomeLightDiv / lightLambda) * (float)sampleTime * displayRefreshStepSkips;
		}
		
		clockTime += sampleTime;
		
		if (clockIgnoreOnReset > 0l)
			clockIgnoreOnReset--;
	}// step()
	
	
	inline void performPending(int chan, float lightTime) {
		if (pendingOp == 1) {
			if (!getGate(chan)) {
				setGate(chan);// bank and indexStep are global
				bigPulse.trigger(0.001f);
			}
			setCV(chan, pendingCV);
			bigLightPulse.trigger(lightTime);
		}
		else {
			clearGate(chan);// bank and indexStep are global
		}
		pendingOp = 0;
	}
};


struct BigButtonTestWidget : ModuleWidget {


	struct ChanDisplayWidget : TransparentWidget {
		int *chan;
		std::shared_ptr<Font> font;
		
		ChanDisplayWidget() {
			font = Font::load(assetPlugin(plugin, "res/fonts/Segment14.ttf"));
		}

		void draw(NVGcontext *vg) override {
			NVGcolor textColor = prepareDisplay(vg, &box, 18);
			nvgFontFaceId(vg, font->handle);
			//nvgTextLetterSpacing(vg, 2.5);

			Vec textPos = Vec(6, 24);
			nvgFillColor(vg, nvgTransRGBA(textColor, 16));
			nvgText(vg, textPos.x, textPos.y, "~", NULL);
			nvgFillColor(vg, textColor);
			char displayStr[2];
			snprintf(displayStr, 2, "%1u", (unsigned) (*chan + 1) );
			nvgText(vg, textPos.x, textPos.y, displayStr, NULL);
		}
	};

	struct StepsDisplayWidget : TransparentWidget {
		int *len;
		std::shared_ptr<Font> font;
		
		StepsDisplayWidget() {
			font = Font::load(assetPlugin(plugin, "res/fonts/Segment14.ttf"));
		}

		void draw(NVGcontext *vg) override {
			NVGcolor textColor = prepareDisplay(vg, &box, 18);
			nvgFontFaceId(vg, font->handle);
			//nvgTextLetterSpacing(vg, 2.5);

			Vec textPos = Vec(6, 24);
			nvgFillColor(vg, nvgTransRGBA(textColor, 16));
			nvgText(vg, textPos.x, textPos.y, "~~", NULL);
			nvgFillColor(vg, textColor);
			char displayStr[3];
			snprintf(displayStr, 3, "%2u", (unsigned) *len );
			nvgText(vg, textPos.x, textPos.y, displayStr, NULL);
		}
	};
	
	struct PanelThemeItem : MenuItem {
		BigButtonTest *module;
		int theme;
		void onAction(EventAction &e) override {
			module->panelTheme = theme;
		}
		void step() override {
			rightText = (module->panelTheme == theme) ? "✔" : "";
		}
	};
	struct MetronomeItem : MenuItem {
		BigButtonTest *module;
		int div;
		void onAction(EventAction &e) override {
			module->metronomeDiv = div;
		}
		void step() override {
			rightText = (module->metronomeDiv == div) ? "✔" : "";
		}
	};
	struct FillModeItem : MenuItem {
		BigButtonTest *module;
		void onAction(EventAction &e) override {
			module->writeFillsToMemory = !module->writeFillsToMemory;
		}
	};
	struct QuantizeBigItem : MenuItem {
		BigButtonTest *module;
		void onAction(EventAction &e) override {
			module->quantizeBig = !module->quantizeBig;
		}
	};
	Menu *createContextMenu() override {
		Menu *menu = ModuleWidget::createContextMenu();

		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		BigButtonTest *module = dynamic_cast<BigButtonTest*>(this->module);
		assert(module);

		MenuLabel *themeLabel = new MenuLabel();
		themeLabel->text = "Panel Theme";
		menu->addChild(themeLabel);

		PanelThemeItem *lightItem = new PanelThemeItem();
		lightItem->text = lightPanelID;// ImpromptuModular.hpp
		lightItem->module = module;
		lightItem->theme = 0;
		menu->addChild(lightItem);

		PanelThemeItem *darkItem = new PanelThemeItem();
		std::string hotRodLabel = " Hot-rod";
		hotRodLabel.insert(0, darkPanelID);// ImpromptuModular.hpp
		darkItem->text = hotRodLabel;
		darkItem->module = module;
		darkItem->theme = 1;
		menu->addChild(darkItem);

		menu->addChild(new MenuLabel());// empty line
		
		MenuLabel *settingsLabel = new MenuLabel();
		settingsLabel->text = "Settings";
		menu->addChild(settingsLabel);

		FillModeItem *fillItem = MenuItem::create<FillModeItem>("Write Fills to Memory", CHECKMARK(module->writeFillsToMemory));
		fillItem->module = module;
		menu->addChild(fillItem);		
		
		QuantizeBigItem *qtzBigItem = MenuItem::create<QuantizeBigItem>("Quantize Big Button (temporally)", CHECKMARK(module->quantizeBig));
		qtzBigItem->module = module;
		menu->addChild(qtzBigItem);		
		
		menu->addChild(new MenuLabel());// empty line
		
		MenuLabel *metronomeLabel = new MenuLabel();
		metronomeLabel->text = "Metronome light";
		menu->addChild(metronomeLabel);

		MetronomeItem *met1Item = MenuItem::create<MetronomeItem>("Every clock", CHECKMARK(module->metronomeDiv == 1));
		met1Item->module = module;
		met1Item->div = 1;
		menu->addChild(met1Item);

		MetronomeItem *met2Item = MenuItem::create<MetronomeItem>("/2", CHECKMARK(module->metronomeDiv == 2));
		met2Item->module = module;
		met2Item->div = 2;
		menu->addChild(met2Item);

		MetronomeItem *met4Item = MenuItem::create<MetronomeItem>("/4", CHECKMARK(module->metronomeDiv == 4));
		met4Item->module = module;
		met4Item->div = 4;
		menu->addChild(met4Item);

		MetronomeItem *met8Item = MenuItem::create<MetronomeItem>("/8", CHECKMARK(module->metronomeDiv == 8));
		met8Item->module = module;
		met8Item->div = 8;
		menu->addChild(met8Item);

		MetronomeItem *met1000Item = MenuItem::create<MetronomeItem>("Full length", CHECKMARK(module->metronomeDiv == 1000));
		met1000Item->module = module;
		met1000Item->div = 1000;
		menu->addChild(met1000Item);

		return menu;
	}	
	
	
	BigButtonTestWidget(BigButtonTest *module) : ModuleWidget(module) {
		// Main panel from Inkscape
        DynamicSVGPanel *panel = new DynamicSVGPanel();
        panel->addPanel(SVG::load(assetPlugin(plugin, "res/light/BigButtonTest.svg")));
        panel->addPanel(SVG::load(assetPlugin(plugin, "res/dark/BigButtonTest_dark.svg")));
        box.size = panel->box.size;
        panel->mode = &module->panelTheme;
        addChild(panel);

		// Screws
		addChild(createDynamicScrew<IMScrew>(Vec(15, 0), &module->panelTheme));
		addChild(createDynamicScrew<IMScrew>(Vec(box.size.x-30, 0), &module->panelTheme));
		addChild(createDynamicScrew<IMScrew>(Vec(15, 365), &module->panelTheme));
		addChild(createDynamicScrew<IMScrew>(Vec(box.size.x-30, 365), &module->panelTheme));

		
		
		// Column rulers (horizontal positions)
		static const int rowRuler0 = 49;// outputs and leds
		static const int colRulerCenter = 115;// not real center, but pos so that a jack would be centered
		static const int offsetChanOutX = 20;
		static const int colRulerT0 = colRulerCenter - offsetChanOutX * 5;
		static const int colRulerT1 = colRulerCenter - offsetChanOutX * 3;
		static const int colRulerT2 = colRulerCenter - offsetChanOutX * 1;
		static const int colRulerT3 = colRulerCenter + offsetChanOutX * 1;
		static const int colRulerT4 = colRulerCenter + offsetChanOutX * 3;
		static const int colRulerT5 = colRulerCenter + offsetChanOutX * 5;
		static const int ledOffsetY = 28;
		
		// Outputs
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT0, rowRuler0), Port::OUTPUT, module, BigButtonTest::CHAN_OUTPUTS + 0, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT1, rowRuler0), Port::OUTPUT, module, BigButtonTest::CHAN_OUTPUTS + 1, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT2, rowRuler0), Port::OUTPUT, module, BigButtonTest::CHAN_OUTPUTS + 2, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT3, rowRuler0), Port::OUTPUT, module, BigButtonTest::CHAN_OUTPUTS + 3, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT4, rowRuler0), Port::OUTPUT, module, BigButtonTest::CHAN_OUTPUTS + 4, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT5, rowRuler0), Port::OUTPUT, module, BigButtonTest::CHAN_OUTPUTS + 5, &module->panelTheme));
		// LEDs
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(colRulerT0 + offsetMediumLight - 1, rowRuler0 + ledOffsetY + offsetMediumLight), module, BigButtonTest::CHAN_LIGHTS + 0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(colRulerT1 + offsetMediumLight - 1, rowRuler0 + ledOffsetY + offsetMediumLight), module, BigButtonTest::CHAN_LIGHTS + 2));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(colRulerT2 + offsetMediumLight - 1, rowRuler0 + ledOffsetY + offsetMediumLight), module, BigButtonTest::CHAN_LIGHTS + 4));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(colRulerT3 + offsetMediumLight - 1, rowRuler0 + ledOffsetY + offsetMediumLight), module, BigButtonTest::CHAN_LIGHTS + 6));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(colRulerT4 + offsetMediumLight - 1, rowRuler0 + ledOffsetY + offsetMediumLight), module, BigButtonTest::CHAN_LIGHTS + 8));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(colRulerT5 + offsetMediumLight - 1, rowRuler0 + ledOffsetY + offsetMediumLight), module, BigButtonTest::CHAN_LIGHTS + 10));

		
		
		static const int rowRuler1 = rowRuler0 + 72;// clk, chan and big CV
		static const int knobCVjackOffsetX = 52;
		
		// Clock input
		addInput(createDynamicPort<IMPort>(Vec(colRulerT0, rowRuler1), Port::INPUT, module, BigButtonTest::CLK_INPUT, &module->panelTheme));
		// Chan knob and jack
		addParam(createDynamicParam<IMSixPosBigKnob>(Vec(colRulerCenter + offsetIMBigKnob, rowRuler1 + offsetIMBigKnob), module, BigButtonTest::CHAN_PARAM, 0.0f, 6.0f - 1.0f, 0.0f, &module->panelTheme));		
		addInput(createDynamicPort<IMPort>(Vec(colRulerCenter - knobCVjackOffsetX, rowRuler1), Port::INPUT, module, BigButtonTest::CHAN_INPUT, &module->panelTheme));
		// Chan display
		ChanDisplayWidget *displayChan = new ChanDisplayWidget();
		displayChan->box.pos = Vec(colRulerCenter + 43, rowRuler1 + vOffsetDisplay - 1);
		displayChan->box.size = Vec(24, 30);// 1 character
		displayChan->chan = &module->chan;
		addChild(displayChan);	
		// Length display
		StepsDisplayWidget *displaySteps = new StepsDisplayWidget();
		displaySteps->box.pos = Vec(colRulerT5 - 17, rowRuler1 + vOffsetDisplay - 1);
		displaySteps->box.size = Vec(40, 30);// 2 characters
		displaySteps->len = &module->len;
		addChild(displaySteps);	


		
		static const int rowRuler2 = rowRuler1 + 50;// len and rnd
		static const int lenAndRndKnobOffsetX = 90;
		
		// Len knob and jack
		addParam(createDynamicParam<IMBigSnapKnob>(Vec(colRulerCenter - lenAndRndKnobOffsetX + offsetIMBigKnob, rowRuler2 + offsetIMBigKnob), module, BigButtonTest::LEN_PARAM, 0.0f, 64.0f - 1.0f, 32.0f - 1.0f, &module->panelTheme));		
		addInput(createDynamicPort<IMPort>(Vec(colRulerCenter - lenAndRndKnobOffsetX + knobCVjackOffsetX, rowRuler2), Port::INPUT, module, BigButtonTest::LEN_INPUT, &module->panelTheme));
		// Rnd knob and jack
		addParam(createDynamicParam<IMBigSnapKnob>(Vec(colRulerCenter + lenAndRndKnobOffsetX + offsetIMBigKnob, rowRuler2 + offsetIMBigKnob), module, BigButtonTest::RND_PARAM, 0.0f, 100.0f, 0.0f, &module->panelTheme));		
		addInput(createDynamicPort<IMPort>(Vec(colRulerCenter + lenAndRndKnobOffsetX - knobCVjackOffsetX, rowRuler2), Port::INPUT, module, BigButtonTest::RND_INPUT, &module->panelTheme));


		
		static const int rowRuler3 = rowRuler2 + 35;// bank
		static const int rowRuler4 = rowRuler3 + 22;// clear and del
		static const int rowRuler5 = rowRuler4 + 52;// reset and fill
		static const int clearAndDelButtonOffsetX = (colRulerCenter - colRulerT0) / 2 + 8;
		static const int knobCVjackOffsetY = 40;
		
		// Bank button and jack
		addParam(createDynamicParam<IMBigPushButton>(Vec(colRulerCenter + offsetCKD6b, rowRuler3 + offsetCKD6b), module, BigButtonTest::BANK_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));	
		addInput(createDynamicPort<IMPort>(Vec(colRulerCenter, rowRuler3 + knobCVjackOffsetY), Port::INPUT, module, BigButtonTest::BANK_INPUT, &module->panelTheme));
		// Clear button and jack
		addParam(createDynamicParam<IMBigPushButton>(Vec(colRulerCenter - clearAndDelButtonOffsetX + offsetCKD6b, rowRuler4 + offsetCKD6b), module, BigButtonTest::CLEAR_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));	
		addInput(createDynamicPort<IMPort>(Vec(colRulerCenter - clearAndDelButtonOffsetX, rowRuler4 + knobCVjackOffsetY), Port::INPUT, module, BigButtonTest::CLEAR_INPUT, &module->panelTheme));
		// Del button and jack
		addParam(createDynamicParam<IMBigPushButton>(Vec(colRulerCenter + clearAndDelButtonOffsetX + offsetCKD6b, rowRuler4 + offsetCKD6b), module, BigButtonTest::DEL_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));	
		addInput(createDynamicPort<IMPort>(Vec(colRulerCenter + clearAndDelButtonOffsetX, rowRuler4 + knobCVjackOffsetY), Port::INPUT, module, BigButtonTest::DEL_INPUT, &module->panelTheme));
		// Reset button and jack
		addParam(createDynamicParam<IMBigPushButton>(Vec(colRulerT0 + offsetCKD6b, rowRuler5 + offsetCKD6b), module, BigButtonTest::RESET_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));	
		addInput(createDynamicPort<IMPort>(Vec(colRulerT0, rowRuler5 + knobCVjackOffsetY), Port::INPUT, module, BigButtonTest::RESET_INPUT, &module->panelTheme));
		// Fill button and jack
		addParam(createDynamicParam<IMBigPushButton>(Vec(colRulerT5 + offsetCKD6b, rowRuler5 + offsetCKD6b), module, BigButtonTest::FILL_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));	
		addInput(createDynamicPort<IMPort>(Vec(colRulerT5, rowRuler5 + knobCVjackOffsetY), Port::INPUT, module, BigButtonTest::FILL_INPUT, &module->panelTheme));

		// And now time for... BIG BUTTON!
		addChild(createLight<GiantLight<RedLight>>(Vec(colRulerCenter + offsetLEDbezelBig - offsetLEDbezelLight*2.0f, rowRuler5 + 26 + offsetLEDbezelBig - offsetLEDbezelLight*2.0f), module, BigButtonTest::BIG_LIGHT));
		addParam(createParam<LEDBezelBig>(Vec(colRulerCenter + offsetLEDbezelBig, rowRuler5 + 26 + offsetLEDbezelBig), module, BigButtonTest::BIG_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(createLight<GiantLight2<RedLight>>(Vec(colRulerCenter + offsetLEDbezelBig - offsetLEDbezelLight*2.0f + 9, rowRuler5 + 26 + offsetLEDbezelBig - offsetLEDbezelLight*2.0f + 9), module, BigButtonTest::BIGC_LIGHT));
		// Big input
		addInput(createDynamicPort<IMPort>(Vec(colRulerCenter - clearAndDelButtonOffsetX, rowRuler5 + knobCVjackOffsetY), Port::INPUT, module, BigButtonTest::BIG_INPUT, &module->panelTheme));
		// Metronome light
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(colRulerCenter + clearAndDelButtonOffsetX + offsetMediumLight - 1, rowRuler5 + knobCVjackOffsetY + offsetMediumLight - 1), module, BigButtonTest::METRONOME_LIGHT + 0));

		
		
		// CV Outputs
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT0 + 255, rowRuler0), Port::OUTPUT, module, BigButtonTest::CV_OUTPUTS + 0, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT1 + 255, rowRuler0), Port::OUTPUT, module, BigButtonTest::CV_OUTPUTS + 1, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT2 + 255, rowRuler0), Port::OUTPUT, module, BigButtonTest::CV_OUTPUTS + 2, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT3 + 255, rowRuler0), Port::OUTPUT, module, BigButtonTest::CV_OUTPUTS + 3, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT4 + 255, rowRuler0), Port::OUTPUT, module, BigButtonTest::CV_OUTPUTS + 4, &module->panelTheme));
		addOutput(createDynamicPort<IMPort>(Vec(colRulerT5 + 255, rowRuler0), Port::OUTPUT, module, BigButtonTest::CV_OUTPUTS + 5, &module->panelTheme));

		// CV Inputs
		addInput(createDynamicPort<IMPort>(Vec(colRulerCenter + 255 - 6, rowRuler2 - 20), Port::INPUT, module, BigButtonTest::CV_INPUT, &module->panelTheme));		
	}
};

Model *modelBigButtonTest = Model::create<BigButtonTest, BigButtonTestWidget>("Impromptu Modular", "Big-Button-Test", "SEQ - Big-Button-Test", SEQUENCER_TAG);

/*CHANGE LOG

0.6.11:
add channel display
add cv (1 input, 6 outputs)
add big quantize option in settings

0.6.10: 
detect BPM and snap BigButton and Del to nearest beat (with timeout if beat slows down too much or stops).

0.6.8:
created

*/