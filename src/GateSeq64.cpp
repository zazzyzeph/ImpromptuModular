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
#include "PhraseSeqUtil.hpp"


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
		// -- 0.6.9 ^^
		GMODELEFT_PARAM,
		GMODERIGHT_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		RUNCV_INPUT,
		SEQCV_INPUT,
		// -- 0.6.7 ^^
		WRITE_INPUT,
		GATE_INPUT,
		PROB_INPUT,
		WRITE1_INPUT,
		WRITE0_INPUT,
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
		// -- 0.6.9 ^^
		ENUMS(GMODE_LIGHTS, 8 * 2),// room for GreenRed
		NUM_LIGHTS
	};
	
	enum DisplayStateIds {DISP_GATE, DISP_LENGTH, DISP_MODES, DISP_ROW_SEL};
	enum AttributeBitMasksGS {ATT_MSK_PROB = 0xFF, ATT_MSK_GATEP = 0x100, ATT_MSK_GATE = 0x200};
	static const int ATT_MSK_GATEMODE = 0x1C00;// 3 bits
	static const int gateModeShift = 10;
	//										1/4		DUO			D2		D2'			TR1			TR2		TR3 		TRI
	const uint32_t advGateHitMaskGS[8] = {0x00003F, 0x03F03F, 0x03F000, 0xFC0000, 0x00000F, 0x000F00, 0x0F0000, 0x0F0F0F};
	
	// Need to save
	int panelTheme = 0;
	int expansion = 0;
	int pulsesPerStep;// 1 means normal gate mode, alt choices are 4, 6, 12, 24 PPS (Pulses per step)
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
	int attributes[16][64];
	//
	bool resetOnRun;

	// No need to save
	int displayState;
	int stepIndexEdit;
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
	long displayProbInfo;// downward step counter for displayProb feedback
	int sequenceKnob = 0;
	int gateCode[4];
	long revertDisplay;
	long editingPpqn;// 0 when no info, positive downward step counter timer when editing ppqn
	int ppqnCount;
	long blinkCount;// positive upward counter, reset to 0 when max reached

	
	static constexpr float CONFIG_PARAM_INIT_VALUE = 0.0f;// so that module constructor is coherent with widget initialization, since module created before widget
	int stepConfigLast;
	static constexpr float EDIT_PARAM_INIT_VALUE = 1.0f;// so that module constructor is coherent with widget initialization, since module created before widget
	bool editingSequence;
	bool editingSequenceLast;


	SchmittTrigger modesTrigger;
	SchmittTrigger stepTriggers[64];
	SchmittTrigger copyTrigger;
	SchmittTrigger pasteTrigger;
	SchmittTrigger runningTrigger;
	SchmittTrigger clockTrigger;
	SchmittTrigger resetTrigger;
	SchmittTrigger writeTrigger;
	SchmittTrigger write0Trigger;
	SchmittTrigger write1Trigger;
	SchmittTrigger gModeLTrigger;
	SchmittTrigger gModeRTrigger;
	HoldDetect modeHoldDetect;

	
	inline bool getGateA(int attribute) {return (attribute & ATT_MSK_GATE) != 0;}
	inline bool getGate(int seq, int step) {return getGateA(attributes[seq][step]);}
	inline bool getGatePa(int attribute) {return (attribute & ATT_MSK_GATEP) != 0;}
	inline bool getGateP(int seq, int step) {return getGatePa(attributes[seq][step]);}
	inline int getGatePValA(int attribute) {return attribute & ATT_MSK_PROB;}
	inline int getGatePVal(int seq, int step) {return getGatePValA(attributes[seq][step]);}
	inline int getGateAMode(int attribute) {return (attribute & ATT_MSK_GATEMODE) >> gateModeShift;}
	inline int getGateMode(int seq, int step) {return getGateAMode(attributes[seq][step]);}
	inline void setGateMode(int seq, int step, int gateMode) {attributes[seq][step] &= ~ATT_MSK_GATEMODE; attributes[seq][step] |= (gateMode << gateModeShift);}
	inline bool isEditingSequence(void) {return params[EDIT_PARAM].value > 0.5f;}
	
	inline int getAdvGateGS(int ppqnCount, int pulsesPerStep, int gateMode) { 
		uint32_t shiftAmt = ppqnCount * (24 / pulsesPerStep);
		return (int)((advGateHitMaskGS[gateMode] >> shiftAmt) & (uint32_t)0x1);
	}	
	inline int calcGateCode(int attribute, int ppqnCount, int pulsesPerStep) {
		// -1 = gate off for whole step, 0 = gate off for current ppqn, 1 = gate on, 2 = clock high
		if (getGatePa(attribute) && !(randomUniform() < ((float)(getGatePValA(attribute))/100.0f)))// randomUniform is [0.0, 1.0), see include/util/common.hpp
			return -1;
		if (!getGateA(attribute))
			return 0;
		if (pulsesPerStep == 1)
			return 2;// clock high
		return getAdvGateGS(ppqnCount, pulsesPerStep, getGateAMode(attribute));
	}		
	inline bool calcGate(int gateCode, SchmittTrigger clockTrigger) {
		if (gateCode < 2) 
			return gateCode == 1;
		return clockTrigger.isHigh();
	}		
	
		
	GateSeq64() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		onReset();
	}

	
	// widgets are not yet created when module is created (and when onReset() is called by constructor)
	// onReset() is also called when right-click initialization of module
	void onReset() override {
		int stepConfig = 1;// 4x16
		if (CONFIG_PARAM_INIT_VALUE > 1.5f)// 1x64
			stepConfig = 4;
		else if (CONFIG_PARAM_INIT_VALUE > 0.5f)// 2x32
			stepConfig = 2;
		stepConfigLast = stepConfig;
		pulsesPerStep = 1;
		running = false;
		runModeSong = MODE_FWD;
		stepIndexEdit = 0;
		phraseIndexEdit = 0;
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
		initRun(stepConfig, true);
		cpBufLength = 16;
		modeCPbuffer = MODE_FWD;
		feedbackCP = 0l;
		displayState = DISP_GATE;
		displayProbInfo = 0l;
		infoCopyPaste = 0l;
		revertDisplay = 0l;
		editingSequence = EDIT_PARAM_INIT_VALUE > 0.5f;
		editingSequenceLast = editingSequence;
		resetOnRun = false;
		editingPpqn = 0l;
		blinkCount = 0l;
	}

	
	// widgets randomized before onRandomize() is called
	void onRandomize() override {
		int stepConfig = 1;// 4x16
		if (params[CONFIG_PARAM].value > 1.5f)// 1x64
			stepConfig = 4;
		else if (params[CONFIG_PARAM].value > 0.5f)// 2x32
			stepConfig = 2;
		running = (randomUniform() > 0.5f);
		runModeSong = randomu32() % 5;
		stepIndexEdit = 0;
		phraseIndexEdit = 0;
		sequence = randomu32() % 16;
		phrases = 1 + (randomu32() % 16);
		for (int i = 0; i < 16; i++) {
			for (int s = 0; s < 64; s++) {
				attributes[i][s] = (randomu32() % 101) | (randomu32() & (ATT_MSK_GATEP | ATT_MSK_GATE | ATT_MSK_GATEMODE));
			}
			runModeSeq[i] = randomu32() % NUM_MODES;
			phrase[i] = randomu32() % 16;
			lengths[i] = 1 + (randomu32() % (16 * stepConfig));
		}
		for (int i = 0; i < 64; i++)
			cpBufAttributes[i] = 50;
		initRun(stepConfig, true);
		cpBufLength = 16;
		modeCPbuffer = MODE_FWD;
		feedbackCP = 0l;
		displayState = DISP_GATE;
		displayProbInfo = 0l;
		infoCopyPaste = 0l;
		revertDisplay = 0l;
		editingSequence = isEditingSequence();
		editingSequenceLast = editingSequence;
		resetOnRun = false;
		editingPpqn = 0l;
	}

	
	void initRun(int stepConfig, bool hard) {// run button activated or run edge in run input jack
		if (hard)	
			phraseIndexRun = (runModeSong == MODE_REV ? phrases - 1 : 0);
		int seq = (editingSequence ? sequence : phrase[phraseIndexRun]);
		if (hard)	
			stepIndexRun = (runModeSeq[seq] == MODE_REV ? lengths[seq] - 1 : 0);
		ppqnCount = 0;
		for (int i = 0; i < 4; i += stepConfig)
			gateCode[i] = calcGateCode(attributes[seq][(i * 16) + stepIndexRun], 0, pulsesPerStep);
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * engineGetSampleRate());
	}
	
	
	json_t *toJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// expansion
		json_object_set_new(rootJ, "expansion", json_integer(expansion));

		// pulsesPerStep
		json_object_set_new(rootJ, "pulsesPerStep", json_integer(pulsesPerStep));

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
		
		// resetOnRun
		json_object_set_new(rootJ, "resetOnRun", json_boolean(resetOnRun));
		
		return rootJ;
	}

	
	// widgets loaded before this fromJson() is called
	void fromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);
		
		// expansion
		json_t *expansionJ = json_object_get(rootJ, "expansion");
		if (expansionJ)
			expansion = json_integer_value(expansionJ);

		// pulsesPerStep
		json_t *pulsesPerStepJ = json_object_get(rootJ, "pulsesPerStep");
		if (pulsesPerStepJ)
			pulsesPerStep = json_integer_value(pulsesPerStepJ);

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
		
		// resetOnRun
		json_t *resetOnRunJ = json_object_get(rootJ, "resetOnRun");
		if (resetOnRunJ)
			resetOnRun = json_is_true(resetOnRunJ);

		// Initialize dependants after everything loaded (widgets already loaded when reach here)
		int stepConfig = 1;// 4x16
		if (params[CONFIG_PARAM].value > 1.5f)// 1x64
			stepConfig = 4;
		else if (params[CONFIG_PARAM].value > 0.5f)// 2x32
			stepConfig = 2;
		stepConfigLast = stepConfig;
		initRun(stepConfig, true);
		editingSequence = isEditingSequence();
		editingSequenceLast = editingSequence;
	}

	
	// Advances the module by 1 audio frame with duration 1.0 / engineGetSampleRate()
	void step() override {
		static const float feedbackCPinitTime = 3.0f;// seconds
		static const float copyPasteInfoTime = 0.5f;// seconds
		static const float displayProbInfoTime = 3.0f;// seconds
		static const float revertDisplayTime = 0.7f;// seconds
		static const float holdDetectTime = 2.0f;// seconds
		float sampleRate = engineGetSampleRate();
		feedbackCPinit = (long) (feedbackCPinitTime * sampleRate);
		long displayProbInfoInit = (long) (displayProbInfoTime * sampleRate);
		long editPpqnTimeInit = (long) (2.5f * sampleRate);
		long blinkCountInit = (long) (1.0f * sampleRate);
		long blinkCountMarker = (long) (0.45f * sampleRate);
		

		
		//********** Buttons, knobs, switches and inputs **********
		
		// Config switch
		int stepConfig = 1;// 4x16
		if (params[CONFIG_PARAM].value > 1.5f)// 1x64
			stepConfig = 4;
		else if (params[CONFIG_PARAM].value > 0.5f)// 2x32
			stepConfig = 2;
		// Config: set lengths to their new max when move switch
		if (stepConfigLast != stepConfig) {
			for (int i = 0; i < 16; i++)
				lengths[i] = 16 * stepConfig;
			stepConfigLast = stepConfig;
		}
				
		// Edit mode		
		editingSequence = isEditingSequence();// true = editing sequence, false = editing song
		if (editingSequenceLast != editingSequence) {
			if (running)
				initRun(stepConfig, true);
			displayState = DISP_GATE;
			editingSequenceLast = editingSequence;
		}

		// Seq CV input
		if (inputs[SEQCV_INPUT].active) {
			sequence = (int) clamp( round(inputs[SEQCV_INPUT].value * 15.0f / 10.0f), 0.0f, 15.0f );
		}

		// Run state button
		if (runningTrigger.process(params[RUN_PARAM].value + inputs[RUNCV_INPUT].value)) {
			running = !running;
			if (running)
				initRun(stepConfig, resetOnRun);
			displayState = DISP_GATE;
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
			}
		}
		
		// Write inputs 
		bool writeTrig = writeTrigger.process(inputs[WRITE_INPUT].value);
		bool write0Trig = write0Trigger.process(inputs[WRITE0_INPUT].value);
		bool write1Trig = write1Trigger.process(inputs[WRITE1_INPUT].value);
		if (writeTrig || write0Trig || write1Trig) {
			if (editingSequence) {
				if (writeTrig) {// higher priority than write0 and write1
					if (inputs[PROB_INPUT].active) {
						attributes[sequence][stepIndexEdit] = clamp( (int)round(inputs[PROB_INPUT].value * 10.0f), 0, 100);
						attributes[sequence][stepIndexEdit] |= ATT_MSK_GATEP;
					}
					else
						attributes[sequence][stepIndexEdit] = 50;
					if (inputs[GATE_INPUT].value >= 1.0f)
						attributes[sequence][stepIndexEdit] |= ATT_MSK_GATE;
				}
				else {// write1 or write0			
					attributes[sequence][stepIndexEdit] = write1Trig ? ATT_MSK_GATE : 0;
				}
				// Autostep (after grab all active inputs)
				stepIndexEdit += 1;
				if (stepIndexEdit >= 64)
					stepIndexEdit = 0;						
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
					}
					else {
						if (!getGateP(sequence, stepPressed)) {// clicked active, but not in prob mode
							if (stepIndexEdit == stepPressed) {// switch to prob mode only if stepPressed on stepIndexEdit
								displayProbInfo = displayProbInfoInit;
								attributes[sequence][stepPressed] |= ATT_MSK_GATEP;
							}
						}
						else {// clicked active, and in prob mode
							if (stepIndexEdit != stepPressed) {// coming from elsewhere, so don't change any states, just show its prob
								displayProbInfo = displayProbInfoInit;
							}
							else {// coming from current step, so turn off
								attributes[sequence][stepPressed] &= ~(ATT_MSK_GATEP | ATT_MSK_GATE);
							}
						}
					}
					stepIndexEdit = stepPressed;
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
						//if (phraseIndexEdit >= phrases) phraseIndexEdit = phrases - 1;// Commented for full edit capabilities
						revertDisplay = (long) (revertDisplayTime * engineGetSampleRate());
					}
					else if (displayState == DISP_MODES) {
						if (col >= 11 && col <= 15)
							runModeSong = col - 11;
					}
					else {
						if (!running) {
							phraseIndexEdit = stepPressed - 48;
						}
					}
				}
			}
		}

		// Mode/Length button
		if (modesTrigger.process(params[MODES_PARAM].value)) {
			if (displayState == DISP_GATE || displayState == DISP_ROW_SEL)
				displayState = DISP_LENGTH;
			else if (displayState == DISP_LENGTH)
				displayState = DISP_MODES;
			else
				displayState = DISP_GATE;
			if (!running) {
				modeHoldDetect.start((long) (holdDetectTime * sampleRate));
			}
		}

		// GateMode buttons
		// Left
		if (gModeLTrigger.process(params[GMODELEFT_PARAM].value)) {
			if (editingSequence) {
				int gmode = getGateMode(sequence, stepIndexEdit);
				gmode--;
				if (gmode < 0)
					gmode = 7;
				setGateMode(sequence, stepIndexEdit, gmode);
				displayProbInfo = displayProbInfoInit;
			}
		}
		// Right
		if (gModeRTrigger.process(params[GMODERIGHT_PARAM].value)) {
			if (editingSequence) {
				int gmode = getGateMode(sequence, stepIndexEdit);
				gmode++;
				if (gmode > 7)
					gmode = 0;
				setGateMode(sequence, stepIndexEdit, gmode);
				displayProbInfo = displayProbInfoInit;
			}
		}
		
		
		// Sequence knob (Main knob)
		float seqParamValue = params[SEQUENCE_PARAM].value;
		int newSequenceKnob = (int)roundf(seqParamValue * 7.0f);
		if (seqParamValue == 0.0f)// true when constructor or fromJson() occured
			sequenceKnob = newSequenceKnob;
		int deltaKnob = newSequenceKnob - sequenceKnob;
		if (deltaKnob != 0) {
			if (abs(deltaKnob) <= 3) {// avoid discontinuous step (initialize for example)
				if (displayProbInfo != 0l && editingSequence) {
						int pval = getGatePVal(sequence, stepIndexEdit);
						pval += deltaKnob * 2;
						if (pval > 100)
							pval = 100;
						if (pval < 0)
							pval = 0;
						attributes[sequence][stepIndexEdit] = pval | (attributes[sequence][stepIndexEdit] & (ATT_MSK_GATE | ATT_MSK_GATEP | ATT_MSK_GATEMODE));
						displayProbInfo = displayProbInfoInit;
				}
				else if (editingPpqn != 0) {
					pulsesPerStep = indexToPps(ppsToIndex(pulsesPerStep) + deltaKnob);// indexToPps() does clamping
					editingPpqn = editPpqnTimeInit;
				}
				else if (displayState == DISP_MODES) {
					if (editingSequence) {
						runModeSeq[sequence] += deltaKnob;
						if (runModeSeq[sequence] < 0) runModeSeq[sequence] = 0;
						if (runModeSeq[sequence] >= NUM_MODES) runModeSeq[sequence] = NUM_MODES - 1;
					}
					else {
						runModeSong += deltaKnob;
						if (runModeSong < 0) runModeSong = 0;
						if (runModeSong >= 5) runModeSong = 5 - 1;
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
						//if (phraseIndexEdit >= phrases) phraseIndexEdit = phrases - 1;
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
		
		
		//********** Clock and reset **********
		
		// Clock
		if (clockTrigger.process(inputs[CLOCK_INPUT].value)) {
			if (running && clockIgnoreOnReset == 0l) {
				ppqnCount++;
				if (ppqnCount >= pulsesPerStep)
					ppqnCount = 0;
				
				int newSeq = sequence;// good value when editingSequence, overwrite if not editingSequence
				if (ppqnCount == 0) {
					if (editingSequence) {
						moveIndexRunMode(&stepIndexRun, lengths[sequence], runModeSeq[sequence], &stepIndexRunHistory);
					}
					else {
						if (moveIndexRunMode(&stepIndexRun, lengths[phrase[phraseIndexRun]], runModeSeq[phrase[phraseIndexRun]], &stepIndexRunHistory)) {
							moveIndexRunMode(&phraseIndexRun, phrases, runModeSong, &phraseIndexRunHistory);
							stepIndexRun = (runModeSeq[phrase[phraseIndexRun]] == MODE_REV ? lengths[phrase[phraseIndexRun]] - 1 : 0);// must always refresh after phraseIndexRun has changed
						}
						newSeq = phrase[phraseIndexRun];
					}
				}
				else {
					if (!editingSequence)
						newSeq = phrase[phraseIndexRun];
				}
				for (int i = 0; i < 4; i += stepConfig) { 
					if (gateCode[i] != -1 || ppqnCount == 0)
						gateCode[i] = calcGateCode(attributes[newSeq][(i * 16) + stepIndexRun], ppqnCount, pulsesPerStep);
				}
				//info("*** calcGateCode of %i on chan 2",gateCode[2]);
			}
		}	
		
		// Reset
		if (resetTrigger.process(inputs[RESET_INPUT].value + params[RESET_PARAM].value)) {
			//stepIndexEdit = 0;
			//sequence = 0;
			initRun(stepConfig, true);// must be after sequence reset
			resetLight = 1.0f;
			displayState = DISP_GATE;
			clockTrigger.reset();
		}
		else
			resetLight -= (resetLight / lightLambda) * engineGetSampleTime();
	
		
		//********** Outputs and lights **********
				
		// Gate outputs
		if (running) {
			for (int i = 0; i < 4; i++)
				outputs[GATE_OUTPUTS + i].value = calcGate(gateCode[i], clockTrigger) ? 10.0f : 0.0f;
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
					// BLINK VERSION
					float stepHereOffset = ((stepIndexRun == col) && running) ? 0.5f : 0.0f;
					if (getGate(sequence, i)) {
						if (getGateP(sequence, i)) {
							if (i == stepIndexEdit)// more orange than yellow
								setGreenRed(STEP_LIGHTS + i * 2, (blinkCount > blinkCountMarker) ? 1.0f : 0.0f, (blinkCount > blinkCountMarker) ? 1.0f : 0.0f);
							else// more yellow
								setGreenRed(STEP_LIGHTS + i * 2, 1.0f - stepHereOffset, 1.0f - stepHereOffset);
						}
						else {
							if (i == stepIndexEdit)
								setGreenRed(STEP_LIGHTS + i * 2, (blinkCount > blinkCountMarker) ? 1.0f : 0.0f, 0.0f);
							else
								setGreenRed(STEP_LIGHTS + i * 2, 1.0f - stepHereOffset, 0.0f);
						}
					}
					else {
						if (i == stepIndexEdit && blinkCount > blinkCountMarker)
							setGreenRed(STEP_LIGHTS + i * 2, 0.05f, 0.0f);
						else
							setGreenRed(STEP_LIGHTS + i * 2, stepHereOffset / 5.0f, 0.0f);
					}
					
					// NO-BLINK VERSION
					/*float stepHereOffset = ((stepIndexRun == col) && running) ? 0.5f : 0.0f;
					if (getGate(sequence, i)) {
						if (getGateP(sequence, i)) {
							if (i == stepIndexEdit)// more orange than yellow
								setGreenRed(STEP_LIGHTS + i * 2, 0.4f, 1.0f - stepHereOffset);
							else// more yellow
								setGreenRed(STEP_LIGHTS + i * 2, 1.0f - stepHereOffset, 1.0f - stepHereOffset);
						}
						else {
							if (i == stepIndexEdit)
								setGreenRed(STEP_LIGHTS + i * 2, 1.0f - stepHereOffset, 0.01f);
							else
								setGreenRed(STEP_LIGHTS + i * 2, 1.0f - stepHereOffset, 0.0f);
						}
					}
					else {
						if (i == stepIndexEdit)
							setGreenRed(STEP_LIGHTS + i * 2, 0.0f, 0.05f);
						else
							setGreenRed(STEP_LIGHTS + i * 2, stepHereOffset / 5.0f, 0.0f);
					}*/				
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
		
		// GateMode lights
		if (editingSequence) {
			int gmode = getGateMode(sequence, stepIndexEdit);
			for (int i = 0; i < 8; i++) {	// TODO green for now, add red for pps requirement no met
				if (i == gmode) {
					setGreenRed(GMODE_LIGHTS + i * 2, 1.0f, 0.0f);
				}
				else
					setGreenRed(GMODE_LIGHTS + i * 2, 0.0f, 0.0f);
			}
		}
		else {
			for (int i = 0; i < 8; i++) 
				setGreenRed(GMODE_LIGHTS + i * 2, 0.0f, 0.0f);
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
		if (modeHoldDetect.process(params[MODES_PARAM].value)) {
			displayState = DISP_GATE;
			editingPpqn = editPpqnTimeInit;
		}
		if (editingPpqn > 0l)
			editingPpqn--;
		if (clockIgnoreOnReset > 0l)
			clockIgnoreOnReset--;
		if (revertDisplay > 0l) {
			if (revertDisplay == 1)
				displayState = DISP_GATE;
			revertDisplay--;
		}
		blinkCount++;
		if (blinkCount >= blinkCountInit)
			blinkCount = 0l;


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
	GateSeq64 *module;
	DynamicSVGPanel *panel;
	int oldExpansion;
	int expWidth = 60;
	IMPort* expPorts[5];
		
	struct SequenceDisplayWidget : TransparentWidget {
		GateSeq64 *module;
		std::shared_ptr<Font> font;
		char displayStr[4];
		//std::string modeLabels[5]={"FWD","REV","PPG","BRN","RND"};
		
		SequenceDisplayWidget() {
			font = Font::load(assetPlugin(plugin, "res/fonts/Segment14.ttf"));
		}
		
		void runModeToStr(int num) {
			if (num >= 0 && num < NUM_MODES)
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
			else if (module->displayProbInfo != 0l) {
				int prob = module->getGatePVal(module->sequence, module->stepIndexEdit);
				if ( prob>= 100)
					snprintf(displayStr, 4, "1,0");
				else if (prob >= 1)
					snprintf(displayStr, 4, ",%2u", (unsigned) prob);
				else
					snprintf(displayStr, 4, "  0");
			}
			else if (module->editingPpqn != 0ul) {
				snprintf(displayStr, 4, "x%2u", (unsigned) module->pulsesPerStep);
			}
			else if (module->displayState == GateSeq64::DISP_LENGTH) {
				if (module->editingSequence)
					snprintf(displayStr, 4, "L%2u", (unsigned) module->lengths[module->sequence]);
				else
					snprintf(displayStr, 4, "L%2u", (unsigned) module->phrases);
			}
			else if (module->displayState == GateSeq64::DISP_MODES) {
				if (module->editingSequence)
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
				if (module->editingSequence)
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
	struct ExpansionItem : MenuItem {
		GateSeq64 *module;
		void onAction(EventAction &e) override {
			module->expansion = module->expansion == 1 ? 0 : 1;
		}
	};
	struct ResetOnRunItem : MenuItem {
		GateSeq64 *module;
		void onAction(EventAction &e) override {
			module->resetOnRun = !module->resetOnRun;
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

		menu->addChild(new MenuLabel());// empty line
		
		MenuLabel *settingsLabel = new MenuLabel();
		settingsLabel->text = "Settings";
		menu->addChild(settingsLabel);
		
		ResetOnRunItem *rorItem = MenuItem::create<ResetOnRunItem>("Reset on Run", CHECKMARK(module->resetOnRun));
		rorItem->module = module;
		menu->addChild(rorItem);
		
		menu->addChild(new MenuLabel());// empty line
		
		MenuLabel *expansionLabel = new MenuLabel();
		expansionLabel->text = "Expansion module";
		menu->addChild(expansionLabel);

		ExpansionItem *expItem = MenuItem::create<ExpansionItem>(expansionMenuLabel, CHECKMARK(module->expansion != 0));
		expItem->module = module;
		menu->addChild(expItem);

		return menu;
	}	
	
	void step() override {
		if(module->expansion != oldExpansion) {
			if (oldExpansion!= -1 && module->expansion == 0) {// if just removed expansion panel, disconnect wires to those jacks
				for (int i = 0; i < 5; i++)
					gRackWidget->wireContainer->removeAllWires(expPorts[i]);
			}
			oldExpansion = module->expansion;		
		}
		box.size.x = panel->box.size.x - (1 - module->expansion) * expWidth;
		Widget::step();
	}

	GateSeq64Widget(GateSeq64 *module) : ModuleWidget(module) {		
		this->module = module;
		oldExpansion = -1;
		
		// Main panel from Inkscape
        panel = new DynamicSVGPanel();
        panel->mode = &module->panelTheme;
		panel->expWidth = &expWidth;
        panel->addPanel(SVG::load(assetPlugin(plugin, "res/light/GateSeq64.svg")));
        panel->addPanel(SVG::load(assetPlugin(plugin, "res/dark/GateSeq64_dark.svg")));
        box.size = panel->box.size;
		box.size.x = box.size.x - (1 - module->expansion) * expWidth;
        addChild(panel);		
		
		// Screws
		addChild(createDynamicScrew<IMScrew>(Vec(15, 0), &module->panelTheme));
		addChild(createDynamicScrew<IMScrew>(Vec(15, 365), &module->panelTheme));
		addChild(createDynamicScrew<IMScrew>(Vec(panel->box.size.x-30, 0), &module->panelTheme));
		addChild(createDynamicScrew<IMScrew>(Vec(panel->box.size.x-30, 365), &module->panelTheme));
		addChild(createDynamicScrew<IMScrew>(Vec(panel->box.size.x-30-expWidth, 0), &module->panelTheme));
		addChild(createDynamicScrew<IMScrew>(Vec(panel->box.size.x-30-expWidth, 365), &module->panelTheme));
		
		
		// ****** Top portion (2 switches and LED button array ******
		
		static const int rowRuler0 = 32;
		static const int spacingRows = 34;
		static const int colRulerSteps = 15;
		static const int spacingSteps = 20;
		static const int spacingSteps4 = 4;
		
		
		// Step LED buttons and GateMode lights
		for (int y = 0; y < 4; y++) {
			int posX = colRulerSteps;
			for (int x = 0; x < 16; x++) {
				addParam(ParamWidget::create<LEDButton>(Vec(posX, rowRuler0 + 8 + y * spacingRows - 4.4f), module, GateSeq64::STEP_PARAMS + y * 16 + x, 0.0f, 1.0f, 0.0f));
				addChild(ModuleLightWidget::create<MediumLight<GreenRedLight>>(Vec(posX + 4.4f, rowRuler0 + 8 + y * spacingRows), module, GateSeq64::STEP_LIGHTS + (y * 16 + x) * 2));
				
				if (y == 3 && ((x & 0x1) == 0)) {
					addChild(ModuleLightWidget::create<SmallLight<GreenRedLight>>(Vec(posX + 3, 169), module, GateSeq64::GMODE_LIGHTS + x));	
				}
				
				posX += spacingSteps;
				if ((x + 1) % 4 == 0)
					posX += spacingSteps4;
			}
		}
					
		
		
		// ****** 5x3 Main bottom half Control section ******
		
		static const int colRulerC0 = 25;
		static const int colRulerC1 = 80;
		static const int colRulerC2 = 114;
		static const int colRulerC3 = 179;
		static const int colRulerC4 = 241;
		static const int rowRulerC0 = 208; 
		static const int rowRulerSpacing = 56;
		static const int rowRulerC1 = rowRulerC0 + rowRulerSpacing;
		static const int rowRulerC2 = rowRulerC1 + rowRulerSpacing;
		
		
		
		// Modes button
		addParam(createDynamicParam<IMBigPushButton>(Vec(colRulerC0 + offsetCKD6b, rowRulerC0 + offsetCKD6b), module, GateSeq64::MODES_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Clock input
		addInput(createDynamicPort<IMPort>(Vec(colRulerC0, rowRulerC1), Port::INPUT, module, GateSeq64::CLOCK_INPUT, &module->panelTheme));
		// Reset CV
		addInput(createDynamicPort<IMPort>(Vec(colRulerC0, rowRulerC2), Port::INPUT, module, GateSeq64::RESET_INPUT, &module->panelTheme));
		
				
		// GateMode buttons
		addParam(ParamWidget::create<TL1105>(Vec(colRulerC1 , rowRulerC0 + offsetTL1105), module, GateSeq64::GMODELEFT_PARAM, 0.0f, 1.0f, 0.0f));
		addParam(ParamWidget::create<TL1105>(Vec(colRulerC1 + 30, rowRulerC0 + offsetTL1105), module, GateSeq64::GMODERIGHT_PARAM, 0.0f, 1.0f, 0.0f));
		// Reset LED bezel and light
		addParam(ParamWidget::create<LEDBezel>(Vec(colRulerC1 + offsetLEDbezel, rowRulerC1 + offsetLEDbezel), module, GateSeq64::RESET_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(ModuleLightWidget::create<MuteLight<GreenLight>>(Vec(colRulerC1 + offsetLEDbezel + offsetLEDbezelLight, rowRulerC1 + offsetLEDbezel + offsetLEDbezelLight), module, GateSeq64::RESET_LIGHT));
		// Run LED bezel and light
		addParam(ParamWidget::create<LEDBezel>(Vec(colRulerC2 + offsetLEDbezel, rowRulerC1 + offsetLEDbezel), module, GateSeq64::RUN_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(ModuleLightWidget::create<MuteLight<GreenLight>>(Vec(colRulerC2 + offsetLEDbezel + offsetLEDbezelLight, rowRulerC1 + offsetLEDbezel + offsetLEDbezelLight), module, GateSeq64::RUN_LIGHT));
		// Seq CV
		addInput(createDynamicPort<IMPort>(Vec(colRulerC1, rowRulerC2), Port::INPUT, module, GateSeq64::SEQCV_INPUT, &module->panelTheme));
		// Run CV
		addInput(createDynamicPort<IMPort>(Vec(colRulerC2, rowRulerC2), Port::INPUT, module, GateSeq64::RUNCV_INPUT, &module->panelTheme));

		
		// Sequence display
		SequenceDisplayWidget *displaySequence = new SequenceDisplayWidget();
		displaySequence->box.pos = Vec(colRulerC3 - 15, rowRulerC0 + 2 + vOffsetDisplay);
		displaySequence->box.size = Vec(55, 30);// 3 characters
		displaySequence->module = module;
		addChild(displaySequence);
		// Sequence knob
		addParam(createDynamicParam<IMBigKnobInf>(Vec(colRulerC3 + 1 + offsetIMBigKnob, rowRulerC1 + offsetIMBigKnob), module, GateSeq64::SEQUENCE_PARAM, -INFINITY, INFINITY, 0.0f, &module->panelTheme));		
		// Copy/paste buttons
		addParam(ParamWidget::create<TL1105>(Vec(colRulerC3 - 10, rowRulerC2 + offsetTL1105), module, GateSeq64::COPY_PARAM, 0.0f, 1.0f, 0.0f));
		addParam(ParamWidget::create<TL1105>(Vec(colRulerC3 + 20, rowRulerC2 + offsetTL1105), module, GateSeq64::PASTE_PARAM, 0.0f, 1.0f, 0.0f));
		

		// Seq/Song selector
		addParam(ParamWidget::create<CKSS>(Vec(colRulerC4 + 2 + hOffsetCKSS, rowRulerC0 - 2 + vOffsetCKSS), module, GateSeq64::EDIT_PARAM, 0.0f, 1.0f, GateSeq64::EDIT_PARAM_INIT_VALUE));
		// Config switch (3 position)
		addParam(ParamWidget::create<CKSSThreeInv>(Vec(colRulerC4 + 2 + hOffsetCKSS, rowRulerC1 - 2 + vOffsetCKSSThree), module, GateSeq64::CONFIG_PARAM, 0.0f, 2.0f, GateSeq64::CONFIG_PARAM_INIT_VALUE));// 0.0f is top position
		// Copy paste mode
		addParam(ParamWidget::create<CKSS>(Vec(colRulerC4 + 2 + hOffsetCKSS, rowRulerC2 + vOffsetCKSS), module, GateSeq64::CPMODE_PARAM, 0.0f, 1.0f, 1.0f));

		// Outputs
		for (int iSides = 0; iSides < 4; iSides++)
			addOutput(createDynamicPort<IMPort>(Vec(311, rowRulerC0 + iSides * 40), Port::OUTPUT, module, GateSeq64::GATE_OUTPUTS + iSides, &module->panelTheme));
		
		// Expansion module
		static const int rowRulerExpTop = 65;
		static const int rowSpacingExp = 60;
		static const int colRulerExp = 497 - 30 - 90;// GS64 is (2+6)HP less than PS32
		addInput(expPorts[0] = createDynamicPort<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 0), Port::INPUT, module, GateSeq64::WRITE_INPUT, &module->panelTheme));
		addInput(expPorts[1] = createDynamicPort<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 1), Port::INPUT, module, GateSeq64::GATE_INPUT, &module->panelTheme));
		addInput(expPorts[2] = createDynamicPort<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 2), Port::INPUT, module, GateSeq64::PROB_INPUT, &module->panelTheme));
		addInput(expPorts[3] = createDynamicPort<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 3), Port::INPUT, module, GateSeq64::WRITE0_INPUT, &module->panelTheme));
		addInput(expPorts[4] = createDynamicPort<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 4), Port::INPUT, module, GateSeq64::WRITE1_INPUT, &module->panelTheme));

	}
};


Model *modelGateSeq64 = Model::create<GateSeq64, GateSeq64Widget>("Impromptu Modular", "Gate-Seq-64", "SEQ - Gate-Seq-64", SEQUENCER_TAG);

/*CHANGE LOG

0.6.10:
add advanced gate mode

0.6.9:
add FW2, FW3 and FW4 run modes for sequences (but not for song)

0.6.7:
add expansion panel with extra CVs for writing steps into the module
allow full edit capabilities in song mode
no reset on run by default, with switch added in context menu
reset does not revert seq or song number to 1

0.6.6:
config and knob bug fixes when loading patch

0.6.5:
swap MODE/LEN so that length happens first (update manual)

0.6.4:
initial release of GS64
*/