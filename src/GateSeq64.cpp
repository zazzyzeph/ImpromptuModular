//***********************************************************************************************
//Gate sequencer module for VCV Rack by Marc Boulé
//
//Based on code from the Fundamental and AudibleInstruments plugins by Andrew Belt 
//and graphics from the Component Library by Wes Milholen 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//Module concept by Nigel Sixsmith and Marc Boulé
//
//Acknowledgements: please see README.md
//***********************************************************************************************


#include "ImpromptuModular.hpp"
#include "dsp/digital.hpp"


struct GateSeq64 : Module {
	enum ParamIds {
		ENUMS(STEP_PARAMS, 64),
		MODES_PARAM,
		RUN_PARAM,
		CONFIG_PARAM,
		COPY_PARAM,
		PASTE_PARAM,
		RESET_PARAM,
		PROB_KNOB_PARAM,// no longer used
		EDIT_PARAM,
		SEQUENCE_PARAM,
		CPMODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		RUNCV_INPUT,
		SEQCV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(GATE_OUTPUTS, 4),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(STEP_LIGHTS, 64 * 2),// room for GreenRed
		P_LIGHT,
		RUN_LIGHT,
		RESET_LIGHT,
		NUM_LIGHTS
	};
	
	enum DisplayStateIds {DISP_GATE, DISP_LENGTH, DISP_MODES, DISP_ROW_SEL};// TODO remove DISP_ROW_SEL and make copy paste like in PSxx
	enum AttributeBitMasks {ATT_MSK_PROB = 0xFF, ATT_MSK_GATEP = 0x100, ATT_MSK_GATE = 0x200};
	
	// Need to save
	int panelTheme = 0;
	bool running;
	int runModeSeq[16];
	int runModeSong;
	//
	int sequence;
	int lengths[16];// values are 1 to 16
	//
	int phrase[16];// This is the song (series of phases; a phrase is a patten number)
	int phrases;//1 to 16
	//	
	bool trig[4] = {false, false, false, false};
	int attributes[16][64];

	// No need to save
	int displayState;
	int stepIndexRun;
	int phraseIndexEdit;	
	int phraseIndexRun;
	int stepIndexRunHistory;// no need to initialize
	int phraseIndexRunHistory;// no need to initialize
	int cpBufAttributes[64] = {};// copy-paste one row or all rows
	int cpBufLength;// copy-paste only one row
	int modeCPbuffer;
	long feedbackCP;// downward step counter for CP feedback
	long infoCopyPaste;// 0 when no info, positive downward step counter timer when copy, negative upward when paste
	float resetLight = 0.0f;
	long feedbackCPinit;// no need to initialize
	int cpInfo;// copy = 1, paste = 2
	long clockIgnoreOnReset;
	const float clockIgnoreOnResetDuration = 0.001f;// disable clock on powerup and reset for 1 ms (so that the first step plays)
	int displayProb;// -1 when prob can not be modified, 0 to 63 when prob can be changed.
	long displayProbInfo;// downward step counter for displayProb feedback
	int probKnob;// INT_MAX when knob not seen yet
	int sequenceKnob;// INT_MAX when knob not seen yet
	bool gateRandomEnable[4] = {};
	long revertDisplay;

	
	SchmittTrigger modesTrigger;
	SchmittTrigger stepTriggers[64];
	SchmittTrigger copyTrigger;
	SchmittTrigger pasteTrigger;
	SchmittTrigger runningTrigger;
	SchmittTrigger clockTrigger;
	SchmittTrigger resetTrigger;
	SchmittTrigger configTrigger;
	SchmittTrigger configTrigger2;
	SchmittTrigger configTrigger3;
	SchmittTrigger editTrigger;
	SchmittTrigger editTriggerInv;

	
	inline bool getGate(int seq, int step) {return (attributes[seq][step] & ATT_MSK_GATE) != 0;}
	inline bool getGateP(int seq, int step) {return (attributes[seq][step] & ATT_MSK_GATEP) != 0;}
	inline int getGatePVal(int seq, int step) {return attributes[seq][step] & ATT_MSK_PROB;}
	inline bool isEditingSequence(void) {return params[EDIT_PARAM].value > 0.5f;}
	inline bool calcGateRandomEnable(bool gateP, int gatePVal) {return (randomUniform() < (((float)(gatePVal))/100.0f)) || !gateP;}// randomUniform is [0.0, 1.0), see include/util/common.hpp
		
		
	GateSeq64() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		onReset();
	}

	
	void onReset() override {
		int stepConfig = 1;// 4x16
		if (params[CONFIG_PARAM].value > 1.5f)// 1x64
			stepConfig = 4;
		else if (params[CONFIG_PARAM].value > 0.5f)// 2x32
			stepConfig = 2;
		running = false;
		runModeSong = MODE_FWD;
		sequence = 0;
		phrases = 4;
		for (int i = 0; i < 16; i++) {
			for (int s = 0; s < 64; s++) {
				attributes[i][s] = 50;
			}
			runModeSeq[i] = MODE_FWD;
			phrase[i] = 0;
			lengths[i] = 16 * stepConfig;
		}
		for (int i = 0; i < 64; i++)
			cpBufAttributes[i] = 50;
		initRun(stepConfig);
		cpBufLength = 16;
		modeCPbuffer = MODE_FWD;
		feedbackCP = 0l;
		displayState = DISP_GATE;
		displayProb = -1;
		displayProbInfo = 0l;
		infoCopyPaste = 0l;
		probKnob = INT_MAX;
		sequenceKnob = INT_MAX;
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * engineGetSampleRate());
		revertDisplay = 0l;
	}

	
	void onRandomize() override {
		int stepConfig = 1;// 4x16
		if (params[CONFIG_PARAM].value > 1.5f)// 1x64
			stepConfig = 4;
		else if (params[CONFIG_PARAM].value > 0.5f)// 2x32
			stepConfig = 2;
		running = (randomUniform() > 0.5f);
		runModeSong = randomu32() % 5;
		sequence = randomu32() % 16;
		phrases = 1 + (randomu32() % 16);
		configTrigger.reset();
		configTrigger2.reset();
		configTrigger3.reset();
		for (int i = 0; i < 16; i++) {
			for (int s = 0; s < 64; s++) {
				attributes[i][s] = (randomu32() % 101) | (randomu32() & (ATT_MSK_GATEP | ATT_MSK_GATE));
			}
			runModeSeq[i] = randomu32() % 5;
			phrase[i] = randomu32() % 16;
			lengths[i] = 1 + (randomu32() % (16 * stepConfig));
		}
		for (int i = 0; i < 64; i++)
			cpBufAttributes[i] = 50;
		initRun(stepConfig);
		cpBufLength = 16;
		modeCPbuffer = MODE_FWD;
		feedbackCP = 0l;
		displayState = DISP_GATE;
		displayProb = -1;
		displayProbInfo = 0l;
		infoCopyPaste = 0l;
		probKnob = INT_MAX;
		sequenceKnob = INT_MAX;
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * engineGetSampleRate());
		revertDisplay = 0l;
	}

	
	void initRun(int stepConfig) {// run button activated or run edge in run input jack
		//stepIndexEdit = 0;
		phraseIndexEdit = 0;
		phraseIndexRun = (runModeSong == MODE_REV ? phrases - 1 : 0);
		for (int i = 0; i < 4; i++)
			gateRandomEnable[i] = false;
		if (isEditingSequence()) {
			stepIndexRun = (runModeSeq[sequence] == MODE_REV ? lengths[sequence] - 1 : 0);
			for (int i = 0; i < 4; i += stepConfig)
				gateRandomEnable[i] = calcGateRandomEnable(getGateP(sequence, (i * 16) + stepIndexRun), getGatePVal(sequence, (i * 16) + stepIndexRun));
		}
		else {
			stepIndexRun = (runModeSeq[phrase[phraseIndexRun]] == MODE_REV ? lengths[phrase[phraseIndexRun]] - 1 : 0);
			for (int i = 0; i < 4; i += stepConfig)
				gateRandomEnable[i] = calcGateRandomEnable(getGateP(phrase[phraseIndexRun], (i * 16) + stepIndexRun), getGatePVal(phrase[phraseIndexRun], (i * 16) + stepIndexRun));
		}
	}
	
	
	void resetModule(int stepConfig) {// reset button on module or reset edge in reset input jack
		sequence = 0;
		initRun(stepConfig);// must be after sequence reset
		resetLight = 1.0f;
		displayState = DISP_GATE;
		clockTrigger.reset();
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * engineGetSampleRate());	
	}
		
	
	json_t *toJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// running
		json_object_set_new(rootJ, "running", json_boolean(running));
		
		// runModeSeq
		json_t *runModeSeqJ = json_array();
		for (int i = 0; i < 16; i++)
			json_array_insert_new(runModeSeqJ, i, json_integer(runModeSeq[i]));
		json_object_set_new(rootJ, "runModeSeq2", runModeSeqJ);

		// runModeSong
		json_object_set_new(rootJ, "runModeSong", json_integer(runModeSong));

		// sequence
		json_object_set_new(rootJ, "sequence", json_integer(sequence));

		// lengths
		json_t *lengthsJ = json_array();
		for (int i = 0; i < 16; i++)
			json_array_insert_new(lengthsJ, i, json_integer(lengths[i]));
		json_object_set_new(rootJ, "lengths", lengthsJ);
	
		// phrase 
		json_t *phraseJ = json_array();
		for (int i = 0; i < 16; i++)
			json_array_insert_new(phraseJ, i, json_integer(phrase[i]));
		json_object_set_new(rootJ, "phrase", phraseJ);

		// phrases
		json_object_set_new(rootJ, "phrases", json_integer(phrases));

		// attributes
		json_t *attributesJ = json_array();
		for (int i = 0; i < 16; i++)
			for (int s = 0; s < 64; s++) {
				json_array_insert_new(attributesJ, s + (i * 64), json_integer(attributes[i][s]));
			}
		json_object_set_new(rootJ, "attributes", attributesJ);
		
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);
		
		// running
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ)
			running = json_is_true(runningJ);
		
		// runModeSeq
		json_t *runModeSeqJ = json_object_get(rootJ, "runModeSeq2");
		if (runModeSeqJ) {
			for (int i = 0; i < 16; i++)
			{
				json_t *runModeSeqArrayJ = json_array_get(runModeSeqJ, i);
				if (runModeSeqArrayJ)
					runModeSeq[i] = json_integer_value(runModeSeqArrayJ);
			}			
		}		
		
		// runModeSong
		json_t *runModeSongJ = json_object_get(rootJ, "runModeSong");
		if (runModeSongJ)
			runModeSong = json_integer_value(runModeSongJ);
		
		// sequence
		json_t *sequenceJ = json_object_get(rootJ, "sequence");
		if (sequenceJ)
			sequence = json_integer_value(sequenceJ);
		
		// lengths
		json_t *lengthsJ = json_object_get(rootJ, "lengths");
		if (lengthsJ) {
			for (int i = 0; i < 16; i++)
			{
				json_t *lengthsArrayJ = json_array_get(lengthsJ, i);
				if (lengthsArrayJ)
					lengths[i] = json_integer_value(lengthsArrayJ);
			}			
		}
		
		// phrase
		json_t *phraseJ = json_object_get(rootJ, "phrase");
		if (phraseJ)
			for (int i = 0; i < 16; i++)
			{
				json_t *phraseArrayJ = json_array_get(phraseJ, i);
				if (phraseArrayJ)
					phrase[i] = json_integer_value(phraseArrayJ);
			}
		
		// phrases
		json_t *phrasesJ = json_object_get(rootJ, "phrases");
		if (phrasesJ)
			phrases = json_integer_value(phrasesJ);
	
		// attributes
		json_t *attributesJ = json_object_get(rootJ, "attributes");
		if (attributesJ) {
			for (int i = 0; i < 16; i++)
				for (int s = 0; s < 64; s++) {
					json_t *attributesArrayJ = json_array_get(attributesJ, s + (i * 64));
					if (attributesArrayJ)
						attributes[i][s] = json_integer_value(attributesArrayJ);
				}
		}
		
		// Initialize dependants after everything loaded
		int stepConfig = 1;// 4x16
		if (params[CONFIG_PARAM].value > 1.5f)// 1x64
			stepConfig = 4;
		else if (params[CONFIG_PARAM].value > 0.5f)// 2x32
			stepConfig = 2;
		initRun(stepConfig);
	}

	
	// Advances the module by 1 audio frame with duration 1.0 / engineGetSampleRate()
	void step() override {
		static const float feedbackCPinitTime = 3.0f;// seconds
		static const float copyPasteInfoTime = 0.5f;// seconds
		static const float displayProbInfoTime = 3.0f;// seconds
		static const float revertDisplayTime = 1.0f;// seconds
		float engineSampleRate = engineGetSampleRate();
		feedbackCPinit = (long) (feedbackCPinitTime * engineSampleRate);
		long displayProbInfoInit = (long) (displayProbInfoTime * engineSampleRate);
		
		//********** Buttons, knobs, switches and inputs **********
		
		bool editingSequence = isEditingSequence();// true = editing sequence, false = editing song
		if ( editTrigger.process(params[EDIT_PARAM].value) || editTriggerInv.process(1.0f - params[EDIT_PARAM].value) ) {
			displayState = DISP_GATE;
			displayProb = -1;
		}

		// Seq CV input
		if (inputs[SEQCV_INPUT].active) {
			sequence = (int) clamp( round(inputs[SEQCV_INPUT].value * 15.0f / 10.0f), 0.0f, 15.0f );
		}

		// Config switch
		int stepConfig = 1;// 4x16
		if (params[CONFIG_PARAM].value > 1.5f)// 1x64
			stepConfig = 4;
		else if (params[CONFIG_PARAM].value > 0.5f)// 2x32
			stepConfig = 2;
		// Config: set lengths to their new max when move switch
		bool configTrigged = configTrigger.process(params[CONFIG_PARAM].value*10.0f - 10.0f) || 
			configTrigger2.process(params[CONFIG_PARAM].value*10.0f) ||
			configTrigger3.process(params[CONFIG_PARAM].value*-5.0f + 10.0f);
		if (configTrigged) {
			for (int i = 0; i < 16; i++)
				lengths[i] = 16 * stepConfig;
			displayProb = -1;
		}
		
		// Run state button
		if (runningTrigger.process(params[RUN_PARAM].value + inputs[RUNCV_INPUT].value)) {
			running = !running;
			if (running)
				initRun(stepConfig);
			displayState = DISP_GATE;
			displayProb = -1;
		}
		
		// Modes button
		if (modesTrigger.process(params[MODES_PARAM].value)) {
			if (displayState == DISP_GATE || displayState == DISP_ROW_SEL)
				displayState = DISP_MODES;
			else if (displayState == DISP_MODES)
				displayState = DISP_LENGTH;
			else
				displayState = DISP_GATE;
			displayProb = -1;
		}
				
		
		// Sequence knob (Main knob)
		int newSequenceKnob = (int)roundf(params[SEQUENCE_PARAM].value*7.0f);
		if (sequenceKnob == INT_MAX)
			sequenceKnob = newSequenceKnob;
		int deltaKnob = newSequenceKnob - sequenceKnob;
		if (deltaKnob != 0) {
			if (abs(deltaKnob) <= 3) {// avoid discontinuous step (initialize for example)
				if (displayProb != -1 && editingSequence) {
						int pval = getGatePVal(sequence, displayProb);
						pval += deltaKnob * 2;
						if (pval > 100)
							pval = 100;
						if (pval < 0)
							pval = 0;
						attributes[sequence][displayProb] = pval | (attributes[sequence][displayProb] & (ATT_MSK_GATE | ATT_MSK_GATEP));
						displayProbInfo = displayProbInfoInit;
				}
				else if (displayState == DISP_MODES) {
					if (editingSequence) {
						runModeSeq[sequence] += deltaKnob;
						if (runModeSeq[sequence] < 0) runModeSeq[sequence] = 0;
						if (runModeSeq[sequence] > 4) runModeSeq[sequence] = 4;
					}
					else {
						runModeSong += deltaKnob;
						if (runModeSong < 0) runModeSong = 0;
						if (runModeSong > 4) runModeSong = 4;
					}
				}
				else if (displayState == DISP_LENGTH) {
					if (editingSequence) {
						lengths[sequence] += deltaKnob;
						if (lengths[sequence] > (16 * stepConfig)) 
							lengths[sequence] = (16 * stepConfig);
						if (lengths[sequence] < 1 ) lengths[sequence] = 1;
					}
					else {
						phrases += deltaKnob;
						if (phrases > 16) phrases = 16;
						if (phrases < 1 ) phrases = 1;
						if (phraseIndexEdit >= phrases) phraseIndexEdit = phrases - 1;
					}
				}
				else if (displayState == DISP_ROW_SEL) {
				}
				else {
					if (editingSequence) {
						if (!inputs[SEQCV_INPUT].active) {
							sequence += deltaKnob;
							if (sequence < 0) sequence = 0;
							if (sequence > 15) sequence = 15;
						}
					}
					else {
						phrase[phraseIndexEdit] += deltaKnob;
						if (phrase[phraseIndexEdit] < 0) phrase[phraseIndexEdit] = 0;
						if (phrase[phraseIndexEdit] > 15) phrase[phraseIndexEdit] = 15;
					}	
				}					
			}
			sequenceKnob = newSequenceKnob;
		}

		// Copy, paste buttons
		bool copyTrigged = copyTrigger.process(params[COPY_PARAM].value);
		bool pasteTrigged = pasteTrigger.process(params[PASTE_PARAM].value);
		if (editingSequence) {
			if (copyTrigged || pasteTrigged) {
				if (displayState == DISP_GATE) {
					if (params[CPMODE_PARAM].value > 0.5f) {// if copy-paste in row mode
						cpInfo = 0;
						if (copyTrigged) cpInfo = 1;
						if (pasteTrigged) cpInfo = 2;
						displayState = DISP_ROW_SEL;
						feedbackCP = feedbackCPinit;
					}
					else {// copy-paste in "all" mode
						if (copyTrigged) {
							for (int i = 0; i < 64; i++)
								cpBufAttributes[i] = attributes[sequence][i];
							cpBufLength = lengths[sequence];
							modeCPbuffer = runModeSeq[sequence];
							infoCopyPaste = (long) (copyPasteInfoTime * engineGetSampleRate());
						}
						else {// paste triggered
							for (int i = 0; i < 64; i++)
								attributes[sequence][i] = cpBufAttributes[i];
							lengths[sequence] = cpBufLength;
							if (lengths[sequence] > 16 * stepConfig)
								lengths[sequence] = 16 * stepConfig;
							runModeSeq[sequence] = modeCPbuffer;
							infoCopyPaste = (long) (-1 * copyPasteInfoTime * engineGetSampleRate());
						}
					}
				}
				else if (displayState == DISP_ROW_SEL) {// abort copy or paste
					displayState = DISP_GATE;
				}
				displayProb = -1;
			}
		}
		
		
		// Step LED button presses
		int row = -1;
		int col = -1;
		int stepPressed = -1;
		for (int i = 0; i < 64; i++) {
			if (stepTriggers[i].process(params[STEP_PARAMS + i].value))
				stepPressed = i;
		}		
		if (stepPressed != -1) {
			if (editingSequence) {
				if (displayState == DISP_LENGTH) {
					col = stepPressed % (16 * stepConfig);
					lengths[sequence] = col + 1;
					revertDisplay = (long) (revertDisplayTime * engineGetSampleRate());
				}
				else if (displayState == DISP_ROW_SEL) {
					row = stepPressed / 16;// copy-paste done on blocks of 16 even when in 2x32 or 1x64 config (and length is not copied)
					if (cpInfo == 1) {// copy
						for (int i = 0; i < 16; i++) {
							cpBufAttributes[i] = attributes[sequence][row * 16 + i];
						}
					}					
					else if (cpInfo == 2) {// paste
						for (int i = 0; i < 16; i++)
							attributes[sequence][row * 16 + i] = cpBufAttributes[i];
					}			
					displayState = DISP_GATE;
				}
				else if (displayState == DISP_MODES) {
				}
				else {
					if (!getGate(sequence, stepPressed)) {// clicked inactive, so turn gate on
						attributes[sequence][stepPressed] |= ATT_MSK_GATE;
						attributes[sequence][stepPressed] &= ~ATT_MSK_GATEP;
						displayProb = -1;
					}
					else {
						if (!getGateP(sequence, stepPressed)) {// clicked active, but not in prob mode
							displayProb = stepPressed;
							displayProbInfo = displayProbInfoInit;
							attributes[sequence][stepPressed] |= ATT_MSK_GATEP;
						}
						else {// clicked active, and in prob mode
							if (displayProb != stepPressed) {// coming from elsewhere, so don't change any states, just show its prob
								displayProb = stepPressed;
								displayProbInfo = displayProbInfoInit;
							}
							else {// coming from current step, so turn off
								attributes[sequence][stepPressed] &= ~(ATT_MSK_GATEP | ATT_MSK_GATE);
								displayProb = -1;
							}
						}
					}
				}
			}
			else {// editing song
				row = stepPressed / 16;
				if (row == 3) {
					col = stepPressed % 16;
					if (displayState == DISP_LENGTH) {
						phrases = col + 1;
						if (phrases > 16) phrases = 16;
						if (phrases < 1 ) phrases = 1;
						if (phraseIndexEdit >= phrases) phraseIndexEdit = phrases - 1;
						revertDisplay = (long) (revertDisplayTime * engineGetSampleRate());
					}
					else if (displayState == DISP_MODES) {
						if (col >= 11 && col <= 15)
							runModeSong = col - 11;
					}
					else {
						if (!running) {
							phraseIndexEdit = stepPressed - 48;
							if (phraseIndexEdit >= phrases)
								phraseIndexEdit = phrases - 1;
						}
					}
				}
			}
		}
		
		
		//********** Clock and reset **********
		
		// Clock
		if (clockTrigger.process(inputs[CLOCK_INPUT].value)) {
			if (running && clockIgnoreOnReset == 0l) {
				for (int i = 0; i < 4; i++)
					gateRandomEnable[i] = false;
				if (editingSequence) {
					moveIndexRunMode(&stepIndexRun, lengths[sequence], runModeSeq[sequence], &stepIndexRunHistory);
					for (int i = 0; i < 4; i += stepConfig)
						gateRandomEnable[i] = calcGateRandomEnable(getGateP(sequence, (i * 16) + stepIndexRun), getGatePVal(sequence, (i * 16) + stepIndexRun));
				}
				else {
					if (moveIndexRunMode(&stepIndexRun, lengths[phrase[phraseIndexRun]], runModeSeq[phrase[phraseIndexRun]], &stepIndexRunHistory)) {
						moveIndexRunMode(&phraseIndexRun, phrases, runModeSong, &phraseIndexRunHistory);
						stepIndexRun = (runModeSeq[phrase[phraseIndexRun]] == MODE_REV ? lengths[phrase[phraseIndexRun]] - 1 : 0);// must always refresh after phraseIndexRun has changed
					}
					for (int i = 0; i < 4; i += stepConfig)
						gateRandomEnable[i] = calcGateRandomEnable(getGateP(phrase[phraseIndexRun], (i * 16) + stepIndexRun), getGatePVal(phrase[phraseIndexRun], (i * 16) + stepIndexRun));
				}
			}
		}	
		
		// Reset
		if (resetTrigger.process(inputs[RESET_INPUT].value + params[RESET_PARAM].value))
			resetModule(stepConfig);
		else
			resetLight -= (resetLight / lightLambda) * engineGetSampleTime();
	
		
		//********** Outputs and lights **********
				
		// Gate outputs
		if (running) {
			int seq = editingSequence ? sequence : phrase[phraseIndexRun];
			bool gateOut[4] = {false, false, false, false};
			for (int i = 0; i < 4; i += stepConfig)
				gateOut[i] = gateRandomEnable[i] && clockTrigger.isHigh() && getGate(seq, (i * 16) + stepIndexRun);
			for (int i = 0; i < 4; i++)
				outputs[GATE_OUTPUTS + i].value = gateOut[i] ? 10.0f : 0.0f;
		}
		else {// not running (no gates, no need to hear anything)
			for (int i = 0; i < 4; i++)
				outputs[GATE_OUTPUTS + i].value = 0.0f;	
		}
		
		// Step LED button lights
		int rowToLight = -1;
		if (displayState == DISP_ROW_SEL) 
			rowToLight = CalcRowToLight(feedbackCP, feedbackCPinit);
		for (int i = 0; i < 64; i++) {
			row = i / (16 * stepConfig);
			if (stepConfig == 2 && row == 1) 
				row++;
			col = i % (16 * stepConfig);
			if (editingSequence) {
				if (displayState == DISP_LENGTH) {
					if (col < (lengths[sequence] - 1))
						setGreenRed(STEP_LIGHTS + i * 2, 0.1f, 0.0f);
					else if (col == (lengths[sequence] - 1))
						setGreenRed(STEP_LIGHTS + i * 2, 1.0f, 0.0f);
					else 
						setGreenRed(STEP_LIGHTS + i * 2, 0.0f, 0.0f);
				}
				else if (displayState == DISP_ROW_SEL) {
					if ((i / 16) == rowToLight)
						setGreenRed(STEP_LIGHTS + i * 2, 1.0f, 0.0f);
					else
						setGreenRed(STEP_LIGHTS + i * 2, 0.0f, 0.0f);
				}
				else {
					float stepHereOffset =  ((stepIndexRun == col) && running) ? 0.5f : 0.0f;
					if (getGate(sequence, i)) {
						if (i == displayProb && getGateP(sequence, i)) 
							setGreenRed(STEP_LIGHTS + i * 2, 0.4f, 1.0f - stepHereOffset);
						else
							setGreenRed(STEP_LIGHTS + i * 2, 1.0f - stepHereOffset, getGateP(sequence, i) ? (1.0f - stepHereOffset) : 0.0f);
					}
					else {
						setGreenRed(STEP_LIGHTS + i * 2, stepHereOffset / 5.0f, 0.0f);
					}				
				}
			}
			else {// editing Song
				if (displayState == DISP_LENGTH) {
					row = i / 16;
					col = i % 16;
					if (row == 3 && col < (phrases - 1))
						setGreenRed(STEP_LIGHTS + i * 2, 0.1f, 0.0f);
					else if (row == 3 && col == (phrases - 1))
						setGreenRed(STEP_LIGHTS + i * 2, 1.0f, 0.0f);
					else 
						setGreenRed(STEP_LIGHTS + i * 2, 0.0f, 0.0f);
				}
				else {
					float green;
					if (running) 
						green = (i == (phraseIndexRun + 48)) ? 1.0f : 0.0f;
					else
						green = (i == (phraseIndexEdit + 48)) ? 1.0f : 0.0f;
					green += ((running && (col == stepIndexRun) && i != (phraseIndexEdit + 48)) ? 0.1f : 0.0f);
					setGreenRed(STEP_LIGHTS + i * 2, clamp(green, 0.0f, 1.0f), 0.0f);
				}				
			}
		}
		
		// Reset light
		lights[RESET_LIGHT].value =	resetLight;	

		// Run lights
		lights[RUN_LIGHT].value = running ? 1.0f : 0.0f;
		
		if (feedbackCP > 0l)			
			feedbackCP--;	
		else
			feedbackCP = feedbackCPinit;// roll over
		
		if (infoCopyPaste != 0l) {
			if (infoCopyPaste > 0l)
				infoCopyPaste --;
			if (infoCopyPaste < 0l)
				infoCopyPaste ++;
		}
		if (displayProbInfo > 0l)
			displayProbInfo--;
		else 
			displayProb = -1;
		if (clockIgnoreOnReset > 0l)
			clockIgnoreOnReset--;
		if (revertDisplay > 0l) {
			if (revertDisplay == 1)
				displayState = DISP_GATE;
			revertDisplay--;
		}
	}// step()
	
	
	void setGreenRed(int id, float green, float red) {
		lights[id + 0].value = green;
		lights[id + 1].value = red;
	}

	int CalcRowToLight(long feedbackCP, long feedbackCPinit) {
		int rowToLight = -1;
		long onDelta = feedbackCPinit / 14;
		long onThreshold;// top based
		
		onThreshold = feedbackCPinit;
		if (feedbackCP < onThreshold && feedbackCP > (onThreshold - onDelta))
			rowToLight = 0;
		else {
			onThreshold = feedbackCPinit * 3 / 4;
			if (feedbackCP < onThreshold && feedbackCP > (onThreshold - onDelta))
				rowToLight = 1;
			else {
				onThreshold = feedbackCPinit * 2 / 4;
				if (feedbackCP < onThreshold && feedbackCP > (onThreshold - onDelta))
					rowToLight = 2;
				else {
					onThreshold = feedbackCPinit * 1 / 4;
					if (feedbackCP < onThreshold && feedbackCP > (onThreshold - onDelta))
						rowToLight = 3;
				}
			}
		}
		return rowToLight;
	}
};// GateSeq64 : module

struct GateSeq64Widget : ModuleWidget {
		
	struct SequenceDisplayWidget : TransparentWidget {
		GateSeq64 *module;
		std::shared_ptr<Font> font;
		char displayStr[4];
		std::string modeLabels[5]={"FWD","REV","PPG","BRN","RND"};
		
		SequenceDisplayWidget() {
			font = Font::load(assetPlugin(plugin, "res/fonts/Segment14.ttf"));
		}
		
		void runModeToStr(int num) {
			if (num >= 0 && num < 5)
				snprintf(displayStr, 4, "%s", modeLabels[num].c_str());
		}

		void draw(NVGcontext *vg) override {
			NVGcolor textColor = prepareDisplay(vg, &box);
			nvgFontFaceId(vg, font->handle);
			//nvgTextLetterSpacing(vg, 2.5);

			Vec textPos = Vec(6, 24);
			nvgFillColor(vg, nvgTransRGBA(textColor, 16));
			nvgText(vg, textPos.x, textPos.y, "~~~", NULL);
			nvgFillColor(vg, textColor);				
			if (module->infoCopyPaste != 0l) {
				if (module->infoCopyPaste > 0l) {// if copy display "CPY"
					snprintf(displayStr, 4, "CPY");
				}
				else {// if paste display "PST"
					snprintf(displayStr, 4, "PST");
				}
			}
			else if (module->displayProb != -1) {
				int prob = module->getGatePVal(module->sequence, module->displayProb);
				if ( prob>= 100)
					snprintf(displayStr, 4, "  1");
				else if (prob >= 1)
					snprintf(displayStr, 4, ",%2u", (unsigned) prob);
				else
					snprintf(displayStr, 4, "  0");
			}
			else if (module->displayState == GateSeq64::DISP_LENGTH) {
				if (module->isEditingSequence())
					snprintf(displayStr, 4, "L%2u", (unsigned) module->lengths[module->sequence]);
				else
					snprintf(displayStr, 4, "L%2u", (unsigned) module->phrases);
			}
			else if (module->displayState == GateSeq64::DISP_MODES) {
				if (module->isEditingSequence())
					runModeToStr(module->runModeSeq[module->sequence]);
				else
					runModeToStr(module->runModeSong);
			}
			else if (module->displayState == GateSeq64::DISP_ROW_SEL) {
				snprintf(displayStr, 4, "CPY");
				if (module->cpInfo == 2)
					snprintf(displayStr, 4, "PST");
			}
			else {
				int dispVal = 0;
				if (module->isEditingSequence())
					dispVal = module->sequence;
				else {
					if (module->running)
						dispVal = module->phrase[module->phraseIndexRun];
					else 
						dispVal = module->phrase[module->phraseIndexEdit];
				}
				snprintf(displayStr, 4, " %2u", (unsigned)(dispVal) + 1 );
			}
			nvgText(vg, textPos.x, textPos.y, displayStr, NULL);
		}
	};	
		
		
	struct PanelThemeItem : MenuItem {
		GateSeq64 *module;
		int theme;
		void onAction(EventAction &e) override {
			module->panelTheme = theme;
		}
		void step() override {
			rightText = (module->panelTheme == theme) ? "✔" : "";
		}
	};
	Menu *createContextMenu() override {
		Menu *menu = ModuleWidget::createContextMenu();

		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		GateSeq64 *module = dynamic_cast<GateSeq64*>(this->module);
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
		darkItem->text = darkPanelID;// ImpromptuModular.hpp
		darkItem->module = module;
		darkItem->theme = 1;
		menu->addChild(darkItem);

		return menu;
	}	
	
	GateSeq64Widget(GateSeq64 *module) : ModuleWidget(module) {
		// Main panel from Inkscape
        DynamicSVGPanel *panel = new DynamicSVGPanel();
        panel->addPanel(SVG::load(assetPlugin(plugin, "res/light/GateSeq64.svg")));
        panel->addPanel(SVG::load(assetPlugin(plugin, "res/dark/GateSeq64_dark.svg")));
        box.size = panel->box.size;
        panel->mode = &module->panelTheme;
        addChild(panel);

		// Screw holes (optical illustion makes screws look oval, remove for now)
		/*addChild(new ScrewHole(Vec(15, 0)));
		addChild(new ScrewHole(Vec(box.size.x-30, 0)));
		addChild(new ScrewHole(Vec(15, 365)));
		addChild(new ScrewHole(Vec(box.size.x-30, 365)));*/
		
		// Screws
		addChild(Widget::create<ScrewSilverRandomRot>(Vec(15, 0)));
		addChild(Widget::create<ScrewSilverRandomRot>(Vec(box.size.x-30, 0)));
		addChild(Widget::create<ScrewSilverRandomRot>(Vec(15, 365)));
		addChild(Widget::create<ScrewSilverRandomRot>(Vec(box.size.x-30, 365)));

		
		
		// ****** Top portion (2 switches and LED button array ******
		
		static const int rowRuler0 = 34;
		static const int spacingRows = 36;
		static const int colRulerSteps = 15;
		static const int spacingSteps = 20;
		static const int spacingSteps4 = 4;
		
		
		// Step LED buttons
		for (int y = 0; y < 4; y++) {
			int posX = colRulerSteps;
			for (int x = 0; x < 16; x++) {
				addParam(ParamWidget::create<LEDButton>(Vec(posX, rowRuler0 + 8 + y * spacingRows - 4.4f), module, GateSeq64::STEP_PARAMS + y * 16 + x, 0.0f, 1.0f, 0.0f));
				addChild(ModuleLightWidget::create<MediumLight<GreenRedLight>>(Vec(posX + 4.4f, rowRuler0 + 8 + y * spacingRows), module, GateSeq64::STEP_LIGHTS + (y * 16 + x) * 2));
				posX += spacingSteps;
				if ((x + 1) % 4 == 0)
					posX += spacingSteps4;
			}
		}
			
		
		
		// ****** 5x3 Main bottom half Control section ******
		
		static const int colRulerC0 = 25;
		static const int colRulerSpacing = 72;
		static const int colRulerC1 = colRulerC0 + colRulerSpacing;
		static const int colRulerC2 = colRulerC1 + colRulerSpacing;
		static const int colRulerC3 = colRulerC2 + colRulerSpacing;
		static const int rowRulerC0 = 204; 
		static const int rowRulerSpacing = 56;
		static const int rowRulerC1 = rowRulerC0 + rowRulerSpacing;
		static const int rowRulerC2 = rowRulerC1 + rowRulerSpacing;
		
		
		// Clock input
		addInput(createDynamicPort<IMPort>(Vec(colRulerC0, rowRulerC0), Port::INPUT, module, GateSeq64::CLOCK_INPUT, &module->panelTheme));
		// Reset CV
		addInput(createDynamicPort<IMPort>(Vec(colRulerC0, rowRulerC1), Port::INPUT, module, GateSeq64::RESET_INPUT, &module->panelTheme));
		// Seq CV
		addInput(createDynamicPort<IMPort>(Vec(colRulerC0, rowRulerC2), Port::INPUT, module, GateSeq64::SEQCV_INPUT, &module->panelTheme));
		
		
		// Seq/Song selector
		addParam(ParamWidget::create<CKSS>(Vec(colRulerC1 + hOffsetCKSS, rowRulerC0 - 2 + vOffsetCKSS), module, GateSeq64::EDIT_PARAM, 0.0f, 1.0f, 1.0f));
		// Reset LED bezel and light
		addParam(ParamWidget::create<LEDBezel>(Vec(colRulerC1 - 17 + offsetLEDbezel, rowRulerC1 + offsetLEDbezel), module, GateSeq64::RESET_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(ModuleLightWidget::create<MuteLight<GreenLight>>(Vec(colRulerC1 - 17 + offsetLEDbezel + offsetLEDbezelLight, rowRulerC1 + offsetLEDbezel + offsetLEDbezelLight), module, GateSeq64::RESET_LIGHT));
		// Run LED bezel and light
		addParam(ParamWidget::create<LEDBezel>(Vec(colRulerC1 + 17 + offsetLEDbezel, rowRulerC1 + offsetLEDbezel), module, GateSeq64::RUN_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(ModuleLightWidget::create<MuteLight<GreenLight>>(Vec(colRulerC1 + 17 + offsetLEDbezel + offsetLEDbezelLight, rowRulerC1 + offsetLEDbezel + offsetLEDbezelLight), module, GateSeq64::RUN_LIGHT));
		// Run CV
		addInput(createDynamicPort<IMPort>(Vec(colRulerC1, rowRulerC2), Port::INPUT, module, GateSeq64::RUNCV_INPUT, &module->panelTheme));

		
		// Sequence display
		SequenceDisplayWidget *displaySequence = new SequenceDisplayWidget();
		displaySequence->box.pos = Vec(colRulerC2 - 15, rowRulerC0 + 2 + vOffsetDisplay);
		displaySequence->box.size = Vec(55, 30);// 3 characters
		displaySequence->module = module;
		addChild(displaySequence);
		// Sequence knob
		addParam(ParamWidget::create<IMBigKnobInf>(Vec(colRulerC2 + 1 + offsetIMBigKnob, rowRulerC1 + offsetIMBigKnob), module, GateSeq64::SEQUENCE_PARAM, -INFINITY, INFINITY, 0.0f));		
		// Config switch (3 position)
		addParam(ParamWidget::create<CKSSThreeInv>(Vec(colRulerC2 + hOffsetCKSS, rowRulerC2 - 2 + vOffsetCKSSThree), module, GateSeq64::CONFIG_PARAM, 0.0f, 2.0f, 0.0f));// 0.0f is top position
		

		// Modes button and light
		addParam(createDynamicParam<IMBigPushButton>(Vec(colRulerC3 + offsetCKD6b, rowRulerC0 + offsetCKD6b), module, GateSeq64::MODES_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Copy/paste buttons
		addParam(ParamWidget::create<TL1105>(Vec(colRulerC3 - 10, rowRulerC1 + offsetTL1105), module, GateSeq64::COPY_PARAM, 0.0f, 1.0f, 0.0f));
		addParam(ParamWidget::create<TL1105>(Vec(colRulerC3 + 20, rowRulerC1 + offsetTL1105), module, GateSeq64::PASTE_PARAM, 0.0f, 1.0f, 0.0f));
		// Copy paste mode
		addParam(ParamWidget::create<CKSS>(Vec(colRulerC3 + 2 + hOffsetCKSS, rowRulerC2 - 3 + vOffsetCKSS), module, GateSeq64::CPMODE_PARAM, 0.0f, 1.0f, 1.0f));

		// Outputs
		for (int iSides = 0; iSides < 4; iSides++)
			addOutput(createDynamicPort<IMPort>(Vec(311, rowRulerC0 + iSides * 40), Port::OUTPUT, module, GateSeq64::GATE_OUTPUTS + iSides, &module->panelTheme));
	}
};


Model *modelGateSeq64 = Model::create<GateSeq64, GateSeq64Widget>("Impromptu Modular", "Gate-Seq-64", "Gate-Seq-64", SEQUENCER_TAG);
