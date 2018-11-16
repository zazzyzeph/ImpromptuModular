//***********************************************************************************************
//Multi-track multi-phrase 32 step sequencer module for VCV Rack by Marc Boulé
//
//Based on code from the Fundamental and AudibleInstruments plugins by Andrew Belt 
//and graphics from the Component Library by Wes Milholen 
//See ./LICENSE.txt for all licenses
//See ./res/fonts/ for font licenses
//
//Module inspired by the SA-100 Stepper Acid sequencer by Transistor Sounds Labs
//
//Acknowledgements: please see README.md
//***********************************************************************************************


#include "ImpromptuModular.hpp"
#include "PhraseSeq32ExUtil.hpp"


struct PhraseSeq32Ex : Module {
	enum ParamIds {
		ATTACH_PARAM,
		LEFT_PARAM,
		RIGHT_PARAM,
		EDIT_PARAM,
		PLAY_PARAM,
		PHRASE_PARAM,
		SEQUENCE_PARAM,
		RUN_PARAM,
		COPY_PARAM,
		PASTE_PARAM,
		RESET_PARAM,
		ENUMS(OCTAVE_PARAM, 7),
		GATE_PARAM,
		SLIDE_BTN_PARAM,
		AUTOSTEP_PARAM,
		ENUMS(KEY_PARAMS, 12),
		LENMODE_PARAM,
		CLKRES_PARAM,
		TRAN_ROT_PARAM,
		GATE_PROB_PARAM,
		TIE_PARAM,// Legato
		CPMODE_PARAM,
		ENUMS(STEP_PHRASE_PARAMS, SequencerKernel::MAX_STEPS),
		KEYNOTE_PARAM,
		KEYGATE_PARAM,
		TRACK_PARAM,
		VEL_KNOB_PARAM,
		ALLSTEPS_PARAM,
		ALLSEQS_PARAM,
		ALLTRACKS_PARAM,
		REPS_PARAM,
		SEQUENCE_PLAY_PARAM,
		COMMIT_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		WRITE_INPUT,
		ENUMS(CV_INPUTS, 4),
		RESET_INPUT,
		CLOCK_INPUT,
		LEFTCV_INPUT,
		RIGHTCV_INPUT,
		RUNCV_INPUT,
		SEQCV_INPUT,
		GATECV_INPUT,
		GATEPCV_INPUT,
		TIEDCV_INPUT,
		SLIDECV_INPUT,
		VEL_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(CV_OUTPUTS, 4),
		ENUMS(VEL_OUTPUTS, 4),
		ENUMS(GATE_OUTPUTS, 4),
		NUM_OUTPUTS
	};
	enum LightIds {
		ATTACH_LIGHT,
		ENUMS(STEP_PHRASE_LIGHTS, SequencerKernel::MAX_STEPS * 2),// room for GreenRed
		ENUMS(OCTAVE_LIGHTS, 7),// octaves 1 to 7
		ENUMS(KEY_LIGHTS, 12 * 2),// room for GreenRed
		RUN_LIGHT,
		RESET_LIGHT,
		ENUMS(GATE_LIGHT, 2),// room for GreenRed
		SLIDE_LIGHT,
		GATE_PROB_LIGHT,
		TIE_LIGHT,
		KEYNOTE_LIGHT,
		ENUMS(KEYGATE_LIGHT, 2),// room for GreenRed
		NUM_LIGHTS
	};
	
	// Constants
	enum EditPSDisplayStateIds {DISP_NORMAL, DISP_MODE, DISP_LENGTH, DISP_REPS, DISP_TRANSPOSE, DISP_ROTATE};
	enum VelDisplayStateIds {DISPV_NORMAL, DISPV_PROBVAL, DISPV_SLIDEVAL};

	// Need to save
	int panelTheme = 0;
	int expansion = 0;
	bool autoseq;
	bool running;
	int sequence;
	bool resetOnRun;
	bool attached;
	SequencerKernel sek[4];

	// No need to save
	int stepIndexEdit;
	int phraseIndexEdit;
	long infoCopyPaste;// 0 when no info, positive downward step counter timer when copy, negative upward when paste
	unsigned long editingGate;// 0 when no edit gate, downward step counter timer when edit gate
	float editingGateCV;// no need to initialize, this goes with editingGate (output this only when editingGate > 0)
	int editingGateKeyLight;// no need to initialize, this goes with editingGate (use this only when editingGate > 0)
	int displayState;
	int displayVState;
	float cvCPbuffer[SequencerKernel::MAX_STEPS];// copy paste buffer for CVs
	Attribute attribCPbuffer[SequencerKernel::MAX_STEPS];
	int phraseCPbuffer[SequencerKernel::MAX_PHRASES];
	int repCPbuffer[SequencerKernel::MAX_PHRASES];
	int lengthCPbuffer;
	int modeCPbuffer;
	int countCP;// number of steps to paste (in case CPMODE_PARAM changes between copy and paste)
	int startCP;
	int rotateOffset;// no need to initialize, this goes with displayMode = DISP_ROTATE
	long clockIgnoreOnReset;
	unsigned long clockPeriod;// counts number of step() calls upward from last clock (reset after clock processed)
	long tiedWarning;// 0 when no warning, positive downward step counter timer when warning
	long revertDisplay;
	bool keyboardEditingGates;// 0 when no info, positive when gate1
	long editingPpqn;// 0 when no info, positive downward step counter timer when editing ppqn
	

	unsigned int lightRefreshCounter = 0;
	float resetLight = 0.0f;
	int sequenceKnob = 0;
	int velocityKnob = 0;
	int phraseKnob = 0;
	SchmittTrigger resetTrigger;
	SchmittTrigger leftTrigger;
	SchmittTrigger rightTrigger;
	SchmittTrigger runningTrigger;
	SchmittTrigger clockTrigger;
	SchmittTrigger octTriggers[7];
	SchmittTrigger octmTrigger;
	SchmittTrigger gate1Trigger;
	SchmittTrigger gateProbTrigger;
	SchmittTrigger slideTrigger;
	SchmittTrigger keyTriggers[12];
	SchmittTrigger writeTrigger;
	SchmittTrigger attachedTrigger;
	SchmittTrigger copyTrigger;
	SchmittTrigger pasteTrigger;
	SchmittTrigger modeTrigger;
	SchmittTrigger rotateTrigger;
	SchmittTrigger transposeTrigger;
	SchmittTrigger tiedTrigger;
	SchmittTrigger stepTriggers[SequencerKernel::MAX_STEPS];
	SchmittTrigger keyNoteTrigger;
	SchmittTrigger keyGateTrigger;
	SchmittTrigger repsTrigger;
	SchmittTrigger clkResTrigger;


	inline bool isEditingSequence(void) {return params[EDIT_PARAM].value > 0.5f;}

	
	PhraseSeq32Ex() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		sek[0].setId(0);
		onReset();
	}

	
	// widgets are not yet created when module is created (and when onReset() is called by constructor)
	// onReset() is also called when right-click initialization of module
	void onReset() override {
		autoseq = false;
		running = false;
		stepIndexEdit = 0;
		phraseIndexEdit = 0;
		sequence = 0;
		for (int phrn = 0; phrn < SequencerKernel::MAX_PHRASES; phrn++) {
			phraseCPbuffer[phrn] = 0;
			repCPbuffer[phrn] = 1;
		}
			
		for (int stepn = 0; stepn < SequencerKernel::MAX_STEPS; stepn++) {
			cvCPbuffer[stepn] = 0.0f;
			attribCPbuffer[stepn].init();
		}
		lengthCPbuffer = SequencerKernel::MAX_STEPS;
		modeCPbuffer = SequencerKernel::MODE_FWD;
		countCP = SequencerKernel::MAX_STEPS;
		startCP = 0;
		editingGate = 0ul;
		infoCopyPaste = 0l;
		displayState = DISP_NORMAL;
		displayVState = DISPV_NORMAL;
		attached = true;
		clockPeriod = 0ul;
		tiedWarning = 0ul;
		revertDisplay = 0l;
		resetOnRun = false;
		keyboardEditingGates = false;
		editingPpqn = 0l;
		
		sek[0].reset();
		initRun(true);
	}
	
	
	void onRandomize() override {
		stepIndexEdit = 0;
		phraseIndexEdit = 0;
		sequence = randomu32() % SequencerKernel::MAX_SEQS;
		
		sek[0].randomize();	
		initRun(true);
	}
	
	
	void initRun(bool hard) {// run button activated or run edge in run input jack
		sek[0].initRun(hard, isEditingSequence(), sequence);
		clockIgnoreOnReset = (long) (clockIgnoreOnResetDuration * engineGetSampleRate());
	}	

	
	json_t *toJson() override {
		json_t *rootJ = json_object();

		// panelTheme
		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		// expansion
		json_object_set_new(rootJ, "expansion", json_integer(expansion));

		// autoseq
		json_object_set_new(rootJ, "autoseq", json_boolean(autoseq));
		
		// running
		json_object_set_new(rootJ, "running", json_boolean(running));
		
		// sequence
		json_object_set_new(rootJ, "sequence", json_integer(sequence));

		// attached
		json_object_set_new(rootJ, "attached", json_boolean(attached));

		// resetOnRun
		json_object_set_new(rootJ, "resetOnRun", json_boolean(resetOnRun));
		
		// stepIndexEdit
		json_object_set_new(rootJ, "stepIndexEdit", json_integer(stepIndexEdit));
	
		// phraseIndexEdit
		json_object_set_new(rootJ, "phraseIndexEdit", json_integer(phraseIndexEdit));

		sek[0].toJson(rootJ);
		
		return rootJ;
	}

	
	void fromJson(json_t *rootJ) override {
		// panelTheme
		json_t *panelThemeJ = json_object_get(rootJ, "panelTheme");
		if (panelThemeJ)
			panelTheme = json_integer_value(panelThemeJ);

		// expansion
		json_t *expansionJ = json_object_get(rootJ, "expansion");
		if (expansionJ)
			expansion = json_integer_value(expansionJ);

		// autoseq
		json_t *autoseqJ = json_object_get(rootJ, "autoseq");
		if (autoseqJ)
			autoseq = json_is_true(autoseqJ);

		// running
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ)
			running = json_is_true(runningJ);
		
		// sequence
		json_t *sequenceJ = json_object_get(rootJ, "sequence");
		if (sequenceJ)
			sequence = json_integer_value(sequenceJ);
				
		// attached
		json_t *attachedJ = json_object_get(rootJ, "attached");
		if (attachedJ)
			attached = json_is_true(attachedJ);
		
		// resetOnRun
		json_t *resetOnRunJ = json_object_get(rootJ, "resetOnRun");
		if (resetOnRunJ)
			resetOnRun = json_is_true(resetOnRunJ);

		// stepIndexEdit
		json_t *stepIndexEditJ = json_object_get(rootJ, "stepIndexEdit");
		if (stepIndexEditJ)
			stepIndexEdit = json_integer_value(stepIndexEditJ);
		
		// phraseIndexEdit
		json_t *phraseIndexEditJ = json_object_get(rootJ, "phraseIndexEdit");
		if (phraseIndexEditJ)
			phraseIndexEdit = json_integer_value(phraseIndexEditJ);
		
		sek[0].fromJson(rootJ);
		
		// Initialize dependants after everything loaded
		initRun(true);
	}


	void step() override {
		const float sampleRate = engineGetSampleRate();
		static const float gateTime = 0.4f;// seconds
		static const float copyPasteInfoTime = 0.5f;// seconds
		static const float revertDisplayTime = 0.7f;// seconds
		static const float tiedWarningTime = 0.7f;// seconds
		static const float editGateLengthTime = 3.5f;// seconds
		
		
		//********** Buttons, knobs, switches and inputs **********
		
		// Edit mode
		const bool editingSequence = isEditingSequence();// true = editing sequence, false = editing song
		
		// Run button
		if (runningTrigger.process(params[RUN_PARAM].value + inputs[RUNCV_INPUT].value)) {// no input refresh here, don't want to introduce startup skew
			running = !running;
			if (running)
				initRun(resetOnRun);
			displayState = DISP_NORMAL;
			displayVState = DISPV_NORMAL;
		}

		if ((lightRefreshCounter & userInputsStepSkipMask) == 0) {
			
			// Seq CV input
			if (inputs[SEQCV_INPUT].active) {
				const float maxSeqIndex = ((float)SequencerKernel::MAX_SEQS) - 1.0f;
				sequence = (int) clamp( round(inputs[SEQCV_INPUT].value * maxSeqIndex / 10.0f), 0.0f, maxSeqIndex );
			}
			
			// Mode CV input
			// if (inputs[MODECV_INPUT].active) {
				// if (editingSequence) {
					// const float maxModeVal = (float)SequencerKernel::NUM_MODES - 1.0f; 
					// sek[0].setRunModeSeq(sequence, (int) clamp( round(inputs[MODECV_INPUT].value * maxModeVal / 10.0f), 0.0f, maxModeVal));
				// }
			// }
			
			// Attach button
			if (attachedTrigger.process(params[ATTACH_PARAM].value)) {
				attached = !attached;
				displayState = DISP_NORMAL;			
				displayVState = DISPV_NORMAL;
			}
			if (running && attached) {
				if (editingSequence)
					stepIndexEdit = sek[0].getStepIndexRun();
				else
					phraseIndexEdit = sek[0].getPhraseIndexRun();
			}
			
			// Copy button
			if (copyTrigger.process(params[COPY_PARAM].value)) {
				startCP = editingSequence ? stepIndexEdit : phraseIndexEdit;
				countCP = editingSequence ? SequencerKernel::MAX_STEPS : SequencerKernel::MAX_PHRASES;
				if (params[CPMODE_PARAM].value > 1.5f)// all
					startCP = 0;
				else if (params[CPMODE_PARAM].value < 0.5f)// 4
					countCP = min(4, countCP - startCP);
				else// 8
					countCP = min(8, countCP - startCP);
				if (editingSequence) {
					sek[0].copySequence(cvCPbuffer, attribCPbuffer, &lengthCPbuffer, &modeCPbuffer, sequence, startCP, countCP);
				}
				else {
					sek[0].copyPhrase(phraseCPbuffer, repCPbuffer, &lengthCPbuffer, startCP, countCP);
				}
				infoCopyPaste = (long) (copyPasteInfoTime * sampleRate / displayRefreshStepSkips);
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}
			// Paste button
			if (pasteTrigger.process(params[PASTE_PARAM].value)) {
				infoCopyPaste = (long) (-1 * copyPasteInfoTime * sampleRate / displayRefreshStepSkips);
				startCP = 0;
				if (countCP <= 8) {
					startCP = editingSequence ? stepIndexEdit : phraseIndexEdit;
					countCP = min(countCP, (editingSequence ? SequencerKernel::MAX_STEPS : SequencerKernel::MAX_PHRASES) - startCP);
				}
				if (editingSequence) {
					if (lengthCPbuffer >= 0) {// non-crossed paste (seq vs song)
						sek[0].pasteSequence(cvCPbuffer, attribCPbuffer, &lengthCPbuffer, &modeCPbuffer, sequence, startCP, countCP);
					}
					else {// crossed paste to seq (seq vs song)
						if (params[CPMODE_PARAM].value > 1.5f) { // ALL
							sek[0].initSequence(sequence);
						}
						else if (params[CPMODE_PARAM].value < 0.5f) {// 4
							sek[0].randomizeCVs(sequence);
						}
						else {// 8
							sek[0].randomizeGates(sequence);
						}
						startCP = 0;// light everything when cross paste
						countCP = SequencerKernel::MAX_STEPS;
						infoCopyPaste *= 2l;
					}
				}
				else {// editing song
					if (lengthCPbuffer < 0) {// non-crossed paste (seq vs song)
						sek[0].pastePhrase(phraseCPbuffer, repCPbuffer, startCP, countCP);
					}
					else {// crossed paste to song (seq vs song)
						if (params[CPMODE_PARAM].value > 1.5f) { // ALL
							sek[0].initSong();
						}
						else if (params[CPMODE_PARAM].value < 0.5f) {// 4 
							sek[0].staircaseSong();
						}
						else {// 8
							sek[0].randomizeSong();
						}
						startCP = 0;// light everything when cross paste
						countCP = SequencerKernel::MAX_PHRASES;// could be MAX_STEPS also since lights used for both
						infoCopyPaste *= 2l;
					}					
				}
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}
			
			// Write input (must be before Left and Right in case route gate simultaneously to Right and Write for example)
			//  (write must be to correct step)
			bool writeTrig = writeTrigger.process(inputs[WRITE_INPUT].value);
			if (writeTrig) {
				if (editingSequence) {
					editingGateCV = sek[0].writeCV(sequence, stepIndexEdit, inputs[CV_INPUTS + 0].value);
					editingGate = (unsigned long) (gateTime * sampleRate / displayRefreshStepSkips);
					editingGateKeyLight = -1;
					// Autostep (after grab all active inputs)
					if (params[AUTOSTEP_PARAM].value > 0.5f) {
						stepIndexEdit = moveIndexEx(stepIndexEdit, stepIndexEdit + 1, SequencerKernel::MAX_STEPS);
						if (stepIndexEdit == 0 && autoseq)
							sequence = moveIndexEx(sequence, sequence + 1, SequencerKernel::MAX_STEPS);
					}
				}
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}
			// Left and right CV inputs
			int delta = 0;
			if (leftTrigger.process(inputs[LEFTCV_INPUT].value)) { 
				delta = -1;
			}
			if (rightTrigger.process(inputs[RIGHTCV_INPUT].value)) {
				delta = +1;
			}
			if (delta != 0) {
				displayVState = DISPV_NORMAL;
				if (displayState != DISP_LENGTH)
					displayState = DISP_NORMAL;
				if (displayState == DISP_LENGTH) {
					if (editingSequence) {
						sek[0].modLength(sequence, delta);
					}
					else {
						sek[0].modPhrases(delta);
					}
				}
				else {
					if (!running || !attached) {// don't move heads when attach and running
						if (editingSequence) {
							stepIndexEdit = moveIndexEx(stepIndexEdit, stepIndexEdit + delta, SequencerKernel::MAX_STEPS);
							if (!sek[0].getTied(sequence,stepIndexEdit)) {// play if non-tied step
								if (!writeTrig) {// in case autostep when simultaneous writeCV and stepCV (keep what was done in Write Input block above)
									editingGate = (unsigned long) (gateTime * sampleRate / displayRefreshStepSkips);
									editingGateCV = sek[0].getCV(sequence, stepIndexEdit);
									editingGateKeyLight = -1;
								}
							}
						}
						else {
							phraseIndexEdit = moveIndexEx(phraseIndexEdit, phraseIndexEdit + delta, SequencerKernel::MAX_PHRASES);
							if (!running)
								sek[0].setPhraseIndexRun(phraseIndexEdit);	
						}						
					}
				}
			}

			// Step button presses
			int stepPressed = -1;
			for (int i = 0; i < SequencerKernel::MAX_STEPS; i++) {
				if (stepTriggers[i].process(params[STEP_PHRASE_PARAMS + i].value))
					stepPressed = i;
			}
			if (stepPressed != -1) {
				if (displayState == DISP_LENGTH) {
					if (editingSequence)
						sek[0].setLength(sequence, stepPressed + 1);
					else
						sek[0].setPhrases(stepPressed + 1);
					revertDisplay = (long) (revertDisplayTime * sampleRate / displayRefreshStepSkips);
				}
				else {
					if (!running || !attached) {// not running or detached
						if (editingSequence) {
							stepIndexEdit = stepPressed;
							if (!sek[0].getTied(sequence,stepIndexEdit)) {// play if non-tied step
								editingGate = (unsigned long) (gateTime * sampleRate / displayRefreshStepSkips);
								editingGateCV = sek[0].getCV(sequence, stepIndexEdit);
								editingGateKeyLight = -1;
							}
						}
					}
					displayState = DISP_NORMAL;
				}
				displayVState = DISPV_NORMAL;
			} 
			
			// Length/Mode button
			if (modeTrigger.process(params[LENMODE_PARAM].value)) {
				if (editingPpqn != 0l)
					editingPpqn = 0l;			
				if (displayState != DISP_LENGTH && displayState != DISP_MODE)
					displayState = DISP_LENGTH;
				else if (displayState == DISP_LENGTH)
					displayState = DISP_MODE;
				else
					displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}
			
			// Clk res button
			if (clkResTrigger.process(params[CLKRES_PARAM].value)) {
				if (editingPpqn != 0l) {
					editingPpqn = 0l;	
					displayState = DISP_NORMAL;
				}					
				else
					editingPpqn = (long) (editGateLengthTime * sampleRate / displayRefreshStepSkips);
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}
			
			// Transpose/Rotate button
			if (transposeTrigger.process(params[TRAN_ROT_PARAM].value)) {
				if (editingSequence) {
					if (displayState != DISP_TRANSPOSE && displayState != DISP_ROTATE) {
						displayState = DISP_TRANSPOSE;
					}
					else if (displayState == DISP_TRANSPOSE) {
						displayState = DISP_ROTATE;
						rotateOffset = 0;
					}
					else 
						displayState = DISP_NORMAL;
				}
				displayVState = DISPV_NORMAL;
			}			

			// Reps button
			if (repsTrigger.process(params[REPS_PARAM].value)) {
				if (!editingSequence) {
					if (displayState != DISP_REPS)
						displayState = DISP_REPS;
					else 
						displayState = DISP_NORMAL;
				}
				displayVState = DISPV_NORMAL;
			}			
			
			// Sequence knob 
			float seqParamValue = params[SEQUENCE_PARAM].value;
			int newSequenceKnob = (int)roundf(seqParamValue * 7.0f);
			if (seqParamValue == 0.0f)// true when constructor or fromJson() occured
				sequenceKnob = newSequenceKnob;
			int deltaKnob = newSequenceKnob - sequenceKnob;
			if (deltaKnob != 0) {
				if (abs(deltaKnob) <= 3) {// avoid discontinuous step (initialize for example)
					if (editingPpqn != 0) {
						sek[0].modPulsesPerStepIndex(deltaKnob);
						if (sek[0].getPulsesPerStep() == 1)
							keyboardEditingGates = false;
						editingPpqn = (long) (editGateLengthTime * sampleRate / displayRefreshStepSkips);
					}
					else if (displayState == DISP_MODE) {
						if (editingSequence) {
							// if (!inputs[MODECV_INPUT].active) {
								sek[0].modRunModeSeq(sequence, deltaKnob);
							// }
						}
						else {
							sek[0].modRunModeSong(deltaKnob);
						}
					}
					else if (displayState == DISP_LENGTH) {
						if (editingSequence) {
							sek[0].modLength(sequence, deltaKnob);
						}
						else {
							sek[0].modPhrases(deltaKnob);
						}
					}
					else if (displayState == DISP_TRANSPOSE) {
						if (editingSequence) {
							sek[0].transposeSeq(sequence, deltaKnob);
						}
					}
					else if (displayState == DISP_ROTATE) {
						if (editingSequence) {
							sek[0].rotateSeq(&rotateOffset, sequence, deltaKnob);
						}						
					}					
					else if (displayState == DISP_REPS) {
						if (!editingSequence) {
							sek[0].modPhraseReps(phraseIndexEdit, deltaKnob);
						}						
					}
					else {// DISP_NORMAL
						if (editingSequence) {
							if (!inputs[SEQCV_INPUT].active) {
								sequence += deltaKnob;
								if (sequence < 0) sequence = 0;
								if (sequence >= SequencerKernel::MAX_SEQS) sequence = (SequencerKernel::MAX_SEQS - 1);
							}
						}
						else {
							sek[0].modPhrase(phraseIndexEdit, deltaKnob);
						}
					}
				}
				sequenceKnob = newSequenceKnob;
			}	
			
			// Velocity knob 
			float velParamValue = params[VEL_KNOB_PARAM].value;
			int newVelocityKnob = (int)roundf(velParamValue * 20.0f);
			if (velParamValue == 0.0f)// true when constructor or fromJson() occured
				velocityKnob = newVelocityKnob;
			int deltaKnob2 = newVelocityKnob - velocityKnob;
			if (deltaKnob2 != 0) {
				if (abs(deltaKnob2) <= 3) {// avoid discontinuous step (initialize for example)
					if (displayVState == DISPV_PROBVAL) {
						if (editingSequence) {
							sek[0].modGatePVal(sequence, stepIndexEdit, deltaKnob2 * 2);
						}
					}
					else if (displayVState == DISPV_SLIDEVAL) {
						if (editingSequence) {
							sek[0].modSlideVal(sequence, stepIndexEdit, deltaKnob2 * 2);
						}
					}
					else {// DISPV_NORMAL
						if (editingSequence) {
							sek[0].modVelocityVal(sequence, stepIndexEdit, deltaKnob2);
						}
					}
				}
				velocityKnob = newVelocityKnob;
			}	

			// Phrase knob 
			float phraseParamValue = params[PHRASE_PARAM].value;
			int newPhraseKnob = (int)roundf(phraseParamValue * 7.0f);
			if (phraseParamValue == 0.0f)// true when constructor or fromJson() occured
				phraseKnob = newPhraseKnob;
			int deltaKnob3 = newPhraseKnob - phraseKnob;
			if (deltaKnob3 != 0) {
				if (abs(deltaKnob3) <= 3) {// avoid discontinuous step (initialize for example)
					if (displayVState == DISP_NORMAL) {
						if (!editingSequence) {
							phraseIndexEdit += deltaKnob3;
							if (phraseIndexEdit < 0) phraseIndexEdit = 0;
							if (phraseIndexEdit >= SequencerKernel::MAX_PHRASES) phraseIndexEdit = (SequencerKernel::MAX_PHRASES - 1);
							if (!running)
								sek[0].setPhraseIndexRun(phraseIndexEdit);
							
						}
					}
				}
				phraseKnob = newPhraseKnob;
			}	
			
			// Octave buttons
			int newOct = -1;
			for (int i = 0; i < 7; i++) {
				if (octTriggers[i].process(params[OCTAVE_PARAM + i].value)) {
					newOct = 6 - i;
					displayState = DISP_NORMAL;
				}
			}
			if (newOct >= 0 && newOct <= 6) {
				if (editingSequence) {
					if (sek[0].getTied(sequence,stepIndexEdit))
						tiedWarning = (long) (tiedWarningTime * sampleRate / displayRefreshStepSkips);
					else {			
						editingGateCV = sek[0].applyNewOctave(sequence, stepIndexEdit, newOct);
						editingGate = (unsigned long) (gateTime * sampleRate / displayRefreshStepSkips);
						editingGateKeyLight = -1;
					}
				}
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}		
			
			// Keyboard buttons
			for (int i = 0; i < 12; i++) {
				if (keyTriggers[i].process(params[KEY_PARAMS + i].value)) {
					if (editingSequence) {
						if (keyboardEditingGates) {
							int newMode = sek[0].keyIndexToGateTypeEx(i);
							if (newMode != -1)
								sek[0].setGateType(sequence, stepIndexEdit, newMode);
							else
								editingPpqn = (long) (editGateLengthTime * sampleRate / displayRefreshStepSkips);
						}
						else if (sek[0].getTied(sequence,stepIndexEdit)) {
							if (params[KEY_PARAMS + i].value > 1.5f)
								stepIndexEdit = moveIndexEx(stepIndexEdit, stepIndexEdit + 1, SequencerKernel::MAX_SEQS);
							else
								tiedWarning = (long) (tiedWarningTime * sampleRate / displayRefreshStepSkips);
						}
						else {	
							editingGateCV = sek[0].applyNewKey(sequence, stepIndexEdit, i);
							editingGate = (unsigned long) (gateTime * sampleRate / displayRefreshStepSkips);
							editingGateKeyLight = -1;
							if (params[KEY_PARAMS + i].value > 1.5f) {// if right-click then move to next step
								stepIndexEdit = moveIndexEx(stepIndexEdit, stepIndexEdit + 1, SequencerKernel::MAX_SEQS);
								editingGateKeyLight = i;
							}
						}						
					}
					displayState = DISP_NORMAL;
					displayVState = DISPV_NORMAL;
				}
			}
			
			// Keyboard mode (note or gate type)
			if (keyNoteTrigger.process(params[KEYNOTE_PARAM].value)) {
				if (editingSequence) {
					keyboardEditingGates = false;
				}
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}
			if (keyGateTrigger.process(params[KEYGATE_PARAM].value)) {
				if (editingSequence) {
					if (sek[0].getPulsesPerStep() == 1) {
						editingPpqn = (long) (editGateLengthTime * sampleRate / displayRefreshStepSkips);
					}
					else {
						keyboardEditingGates = true;
					}
				}
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}

			// Gate, GateProb, Slide and Tied buttons
			if (gate1Trigger.process(params[GATE_PARAM].value + inputs[GATECV_INPUT].value)) {
				if (editingSequence) {
					sek[0].toggleGate(sequence, stepIndexEdit);
				}
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}		
			if (gateProbTrigger.process(params[GATE_PROB_PARAM].value + inputs[GATEPCV_INPUT].value)) {
				displayVState = DISPV_NORMAL;
				if (editingSequence) {
					if (sek[0].getTied(sequence,stepIndexEdit))
						tiedWarning = (long) (tiedWarningTime * sampleRate / displayRefreshStepSkips);
					else {
						sek[0].toggleGateP(sequence, stepIndexEdit);
						if (sek[0].getGateP(sequence,stepIndexEdit))
							displayVState = DISPV_PROBVAL;
					}
				}
				displayState = DISP_NORMAL;
			}		
			if (slideTrigger.process(params[SLIDE_BTN_PARAM].value + inputs[SLIDECV_INPUT].value)) {
				displayVState = DISPV_NORMAL;
				if (editingSequence) {
					if (sek[0].getTied(sequence,stepIndexEdit))
						tiedWarning = (long) (tiedWarningTime * sampleRate / displayRefreshStepSkips);
					else {
						sek[0].toggleSlide(sequence, stepIndexEdit);
						if (sek[0].getSlide(sequence,stepIndexEdit))
							displayVState = DISPV_SLIDEVAL;
					}
				}
				displayState = DISP_NORMAL;
			}		
			if (tiedTrigger.process(params[TIE_PARAM].value + inputs[TIEDCV_INPUT].value)) {
				if (editingSequence) {
					sek[0].toggleTied(sequence, stepIndexEdit);// will clear other attribs if new state is on
				}
				displayState = DISP_NORMAL;
				displayVState = DISPV_NORMAL;
			}		
			
		}// userInputs refresh
		
		
		
		//********** Clock and reset **********
		
		// Clock
		if (clockTrigger.process(inputs[CLOCK_INPUT].value)) {
			if (running && clockIgnoreOnReset == 0l) {
				sek[0].clockStep(editingSequence, sequence, clockPeriod);
			}
			clockPeriod = 0ul;
		}
		clockPeriod++;
		
		// Reset
		if (resetTrigger.process(inputs[RESET_INPUT].value + params[RESET_PARAM].value)) {
			initRun(true);
			resetLight = 1.0f;
			displayState = DISP_NORMAL;
		}
		
		
		//********** Outputs and lights **********
				
		
		
		// CV and gates outputs
		// TODO remove redundancy between getCV here and the one for keyboard/oct lights
		// int seqn = 0;
		// if (editingSequence)
			// seqn = sequence;
		// else {
			// int phrn = 0;
			// if (running)
				// phrn = sek[0].getPhraseIndexRun();
			// else
				// phrn = phraseIndexEdit;
			// seqn = sek[0].getPhrase(phrn);
		// }
		
		// int step0 = 0;
		// if (editingSequence) {
			// if (running)
				// step0 = sek[0].getStepIndexRun();
			// else 
				// step0 = stepIndexEdit;
		// }
		// else 
			// step0 = sek[0].getStepIndexRun();
		
		int seqn = editingSequence ? (sequence) : (sek[0].getPhrase(running ? sek[0].getPhraseIndexRun() : phraseIndexEdit));
		int step0 = editingSequence ? (running ? sek[0].getStepIndexRun() : stepIndexEdit) : (sek[0].getStepIndexRun());
		if (running) {
			bool muteGateA = !editingSequence && ((params[GATE_PARAM].value + inputs[GATECV_INPUT].value) > 0.5f);// live mute
			outputs[CV_OUTPUTS + 0].value = sek[0].getCV(seqn, step0) - sek[0].calcSlideOffset();
			outputs[GATE_OUTPUTS + 0].value = (sek[0].calcGate(clockTrigger, clockPeriod, sampleRate) && !muteGateA) ? 10.0f : 0.0f;
			outputs[VEL_OUTPUTS + 0].value = sek[0].getVelocity(seqn, step0);
		}
		else {// not running 
			outputs[CV_OUTPUTS + 0].value = (editingGate > 0ul) ? editingGateCV : sek[0].getCV(seqn, step0);
			outputs[GATE_OUTPUTS + 0].value = (editingGate > 0ul) ? 10.0f : 0.0f;
			outputs[VEL_OUTPUTS + 0].value = (editingGate > 0ul) ? 10.0f : sek[0].getVelocity(seqn, step0);
		}
		sek[0].decSlideStepsRemain();

		
		lightRefreshCounter++;
		if (lightRefreshCounter >= displayRefreshStepSkips) {
			lightRefreshCounter = 0;
		
			// Step/phrase lights
			if (infoCopyPaste != 0l) {
				for (int i = 0; i < SequencerKernel::MAX_SEQS; i++) {
					if (editingSequence && i >= startCP && i < (startCP + countCP))
						lights[STEP_PHRASE_LIGHTS + (i<<1)].value = 0.5f;// Green when copy interval
					else
						lights[STEP_PHRASE_LIGHTS + (i<<1)].value = 0.0f; // Green (nothing)
					lights[STEP_PHRASE_LIGHTS + (i<<1) + 1].value = 0.0f;// Red (nothing)
				}
			}
			else {
				for (int i = 0; i < SequencerKernel::MAX_SEQS; i++) {
					if (displayState == DISP_LENGTH) {
						if (editingSequence) {
							int seqEnd = sek[0].getLength(sequence) - 1;
							if (i < seqEnd)
								setGreenRed(STEP_PHRASE_LIGHTS + i * 2, 0.1f, 0.0f);
							else if (i == seqEnd)
								setGreenRed(STEP_PHRASE_LIGHTS + i * 2, 1.0f, 0.0f);
							else 
								setGreenRed(STEP_PHRASE_LIGHTS + i * 2, 0.0f, 0.0f);
						}
					}
					else {// normal led display (i.e. not length)
						float red = 0.0f;
						float green = 0.0f;
						// Run cursor (green)
						if (editingSequence)
							green = ((running && (i == sek[0].getStepIndexRun())) ? 1.0f : 0.0f);
						// else {
							// green = ((running && (i == sek[0].getPhraseIndexRun())) ? 1.0f : 0.0f);
							// green += ((running && (i == sek[0].getStepIndexRun()) && i != phraseIndexEdit) ? 0.1f : 0.0f);
							// green = clamp(green, 0.0f, 1.0f);
						// }
						// Edit cursor (red)
						if (editingSequence)
							red = (i == stepIndexEdit ? 1.0f : 0.0f);
						//else
							//red = (i == phraseIndexEdit ? 1.0f : 0.0f);
						
						setGreenRed(STEP_PHRASE_LIGHTS + i * 2, green, red);
					}
				}
			}
		
			// Octave lights
			float octKeyCV = sek[0].getCurrentCV(editingSequence, sequence, stepIndexEdit, phraseIndexEdit);// used for keyboard lights also
			int octLightIndex = (int) floor(octKeyCV + 3.0f);
			for (int i = 0; i < 7; i++) {
				float red = 0.0f;
				if (editingSequence || running) {
					if (tiedWarning > 0l) {
						bool warningFlashState = calcWarningFlash(tiedWarning, (long) (tiedWarningTime * sampleRate / displayRefreshStepSkips));
						red = (warningFlashState && (i == (6 - octLightIndex))) ? 1.0f : 0.0f;
					}
					else				
						red = (i == (6 - octLightIndex) ? 1.0f : 0.0f);// no lights when outside of range
				}
				lights[OCTAVE_LIGHTS + i].value = red;
			}
			
			// Keyboard lights
			octKeyCV += 10.0f;// to properly handle negative note voltages
			int keyLightIndex = (int) clamp(  roundf( (octKeyCV-floor(octKeyCV)) * 12.0f ),  0.0f,  11.0f);
			if (keyboardEditingGates && editingSequence) {
				int modeLightIndex = sek[0].getGateType(sequence, stepIndexEdit);
				for (int i = 0; i < 12; i++) {
					if (i == modeLightIndex)
						setGreenRed(KEY_LIGHTS + i * 2, 1.0f, 1.0f);
					else
						setGreenRed(KEY_LIGHTS + i * 2, 0.0f, i == keyLightIndex ? 0.1f : 0.0f);
				}
			}
			else {
				for (int i = 0; i < 12; i++) {
					float red = 0.0f;
					float green = 0.0f;
					if (editingSequence || running) {
						if (tiedWarning > 0l) {
							bool warningFlashState = calcWarningFlash(tiedWarning, (long) (tiedWarningTime * sampleRate / displayRefreshStepSkips));
							red = (warningFlashState && i == keyLightIndex) ? 1.0f : 0.0f;
						}
						else {
							if (editingGate > 0ul && editingGateKeyLight != -1)
								red = (i == editingGateKeyLight ? ((float) editingGate / (float)(gateTime * sampleRate / displayRefreshStepSkips)) : 0.0f);
							else
								red = (i == keyLightIndex ? 1.0f : 0.0f);
						}
					}
					setGreenRed(KEY_LIGHTS + i * 2, green, red);
				}
			}		

			// Gate, GateProb, Slide and Tied lights 
			if (editingSequence  || running) {
				Attribute attributesVal = sek[0].getCurrentAttribute(editingSequence, sequence, stepIndexEdit, phraseIndexEdit);
				if (!attributesVal.getGate()) 
					setGreenRed(GATE_LIGHT, 0.0f, 0.0f);
				else if (sek[0].getPulsesPerStep() == 1) 
					setGreenRed(GATE_LIGHT, 0.0f, 1.0f);
				else 
					setGreenRed(GATE_LIGHT, 1.0f, 1.0f);
				lights[GATE_PROB_LIGHT].value = attributesVal.getGateP() ? 1.0f : 0.0f;
				lights[SLIDE_LIGHT].value = attributesVal.getSlide() ? 1.0f : 0.0f;
				if (tiedWarning > 0l) {
					bool warningFlashState = calcWarningFlash(tiedWarning, (long) (tiedWarningTime * sampleRate / displayRefreshStepSkips));
					lights[TIE_LIGHT].value = (warningFlashState) ? 1.0f : 0.0f;
				}
				else
					lights[TIE_LIGHT].value = attributesVal.getTied() ? 1.0f : 0.0f;			
			}
			else {
				setGreenRed(GATE_LIGHT, 0.0f, 0.0f);
				lights[GATE_PROB_LIGHT].value = 0.0f;
				lights[SLIDE_LIGHT].value = 0.0f;
				lights[TIE_LIGHT].value = 0.0f;
			}
			
			// Key mode light (note or gate type)
			lights[KEYNOTE_LIGHT].value = (!keyboardEditingGates && editingSequence) ? 10.0f : 0.0f;
			if (!keyboardEditingGates || !editingSequence)
				setGreenRed(KEYGATE_LIGHT, 0.0f, 0.0f);
			else
				setGreenRed(KEYGATE_LIGHT, 1.0f, 1.0f);
			
			// Clk Res light (blinks)
			// long editingPpqnInit = (long) (editGateLengthTime * sampleRate / displayRefreshStepSkips);
			// if ( ((editingPpqn > 0l) && (editingPpqn < (editingPpqnInit / 6l))) ||
				 // ((editingPpqn > (editingPpqnInit * 2l / 6l)) && (editingPpqn < (editingPpqnInit * 3l / 6l))) ||
				 // ((editingPpqn > (editingPpqnInit * 4l / 6l)) && (editingPpqn < (editingPpqnInit * 5l / 6l))) )
				// lights[RES_LIGHT].value = 1.0f;
			// else 
				// lights[RES_LIGHT].value = 0.0f;

			// Attach light
			lights[ATTACH_LIGHT].value = (attached ? 1.0f : 0.0f);			
			
			// Reset light
			lights[RESET_LIGHT].value =	resetLight;
			resetLight -= (resetLight / lightLambda) * engineGetSampleTime() * displayRefreshStepSkips;
			
			// Run light
			lights[RUN_LIGHT].value = running ? 1.0f : 0.0f;

			if (editingGate > 0ul)
				editingGate--;
			if (infoCopyPaste != 0l) {
				if (infoCopyPaste > 0l)
					infoCopyPaste --;
				if (infoCopyPaste < 0l)
					infoCopyPaste ++;
			}
			if (editingPpqn > 0l)
				editingPpqn--;
			if (tiedWarning > 0l)
				tiedWarning--;
			if (revertDisplay > 0l) {
				if (revertDisplay == 1)
					displayState = DISP_NORMAL;
				revertDisplay--;
			}
		}// lightRefreshCounter
				
		if (clockIgnoreOnReset > 0l)
			clockIgnoreOnReset--;
		
	}// step()
	

	inline void setGreenRed(int id, float green, float red) {
		lights[id + 0].value = green;
		lights[id + 1].value = red;
	}
};



struct PhraseSeq32ExWidget : ModuleWidget {
	PhraseSeq32Ex *module;
	DynamicSVGPanel *panel;
	int oldExpansion;
	int expWidth = 60;
	IMPort* expPorts[5];
	
	template <int NUMCHAR>
	struct DisplayWidget : TransparentWidget {// a centered display, must derive from this
		PhraseSeq32Ex *module;
		std::shared_ptr<Font> font;
		char displayStr[NUMCHAR+1];

		DisplayWidget(Vec _pos, Vec _size, PhraseSeq32Ex *_module) {
			box.size = _size;
			box.pos = _pos.minus(_size.div(2));
			module = _module;
			font = Font::load(assetPlugin(plugin, "res/fonts/Segment14.ttf"));
		}
		
		void draw(NVGcontext *vg) override {
			NVGcolor textColor = prepareDisplay(vg, &box, 15);
			nvgFontFaceId(vg, font->handle);
			nvgTextLetterSpacing(vg, -0.5);

			Vec textPos = Vec(4.5f, 19.7f);
			nvgFillColor(vg, nvgTransRGBA(textColor, 16));
			std::string initString(NUMCHAR,'~');
			nvgText(vg, textPos.x, textPos.y, initString.c_str(), NULL);
			nvgFillColor(vg, textColor);
			printText();
			nvgText(vg, textPos.x, textPos.y, displayStr, NULL);
		}
		
		virtual void printText() = 0;
	};
	
	struct VelocityDisplayWidget : DisplayWidget<3> {
		VelocityDisplayWidget(Vec _pos, Vec _size, PhraseSeq32Ex *_module) : DisplayWidget(_pos, _size, _module) {};
		void printText() override {
			bool editingSequence = module->isEditingSequence();
			if (editingSequence) {
				if (module->displayVState == PhraseSeq32Ex::DISPV_PROBVAL) {
					int prob = module->sek[0].getGatePVal(module->sequence, module->stepIndexEdit);
					if ( prob>= 100)
						snprintf(displayStr, 4, "1,0");
					else if (prob >= 10)
						snprintf(displayStr, 4, ",%2u", (unsigned) prob);
					else if (prob >= 1)
						snprintf(displayStr, 4, " ,%1u", (unsigned) prob);
					else
						snprintf(displayStr, 4, "  0");
				}
				else if (module->displayVState == PhraseSeq32Ex::DISPV_SLIDEVAL) {
					int slide = module->sek[0].getSlideVal(module->sequence, module->stepIndexEdit);
					if ( slide>= 100)
						snprintf(displayStr, 4, "1,0");
					else if (slide >= 10)
						snprintf(displayStr, 4, ",%2u", (unsigned) slide);
					else if (slide >= 1)
						snprintf(displayStr, 4, " ,%1u", (unsigned) slide);
					else
						snprintf(displayStr, 4, "  0");
				}
				else {// DISPV_NORMAL
					Attribute attributesVal = module->sek[0].getCurrentAttribute(editingSequence, module->sequence, module->stepIndexEdit, module->phraseIndexEdit);
					snprintf(displayStr, 4, "%3u", (unsigned)(attributesVal.getVelocityVal()));
				}
			}
			else
				snprintf(displayStr, 4, " - ");
		}
	};
	
	struct TrackDisplayWidget : DisplayWidget<2> {
		TrackDisplayWidget(Vec _pos, Vec _size, PhraseSeq32Ex *_module) : DisplayWidget(_pos, _size, _module) {};
		void printText() override {
			snprintf(displayStr, 3, "%2u", (unsigned)(1));
		}
	};
	
	struct PSPlayDisplayWidget : DisplayWidget<5> {
		PSPlayDisplayWidget(Vec _pos, Vec _size, PhraseSeq32Ex *_module) : DisplayWidget(_pos, _size, _module) {};
		void printText() override {
			snprintf(displayStr, 6, "%5u", (unsigned)(1));
		}
	};
	
	struct PSEditDisplayWidget : DisplayWidget<5> {
		PSEditDisplayWidget(Vec _pos, Vec _size, PhraseSeq32Ex *_module) : DisplayWidget(_pos, _size, _module) {};
		void runModeToStr(int num) {
			if (num >= 0 && num < SequencerKernel::NUM_MODES)
				snprintf(displayStr, 6, "  %s", SequencerKernel::modeLabels[num].c_str());
		}
		void printText() override {
			bool editingSequence = module->isEditingSequence();
			if (module->infoCopyPaste != 0l) {
				if (module->infoCopyPaste > 0l)
					snprintf(displayStr, 6, "  CPY");
				else {
					int lenCP = module->lengthCPbuffer;
					float cpMode = module->params[PhraseSeq32Ex::CPMODE_PARAM].value;
					if (editingSequence && lenCP == -1) {// cross paste to seq
						if (cpMode > 1.5f)// All = init
							snprintf(displayStr, 6, "  CLR");
						else if (cpMode < 0.5f)// 4 = random CV
							snprintf(displayStr, 6, "  RCV");
						else// 8 = random gate 1
							snprintf(displayStr, 6, "  RG1");
					}
					else if (!editingSequence && lenCP != -1) {// cross paste to song
						if (cpMode > 1.5f)// All = init
							snprintf(displayStr, 6, "  CLR");
						else if (cpMode < 0.5f)// 4 = increase by 1
							snprintf(displayStr, 6, "  INC");
						else// 8 = random phrases
							snprintf(displayStr, 6, "  RPH");
					}
					else
						snprintf(displayStr, 6, "  PST");
				}
			}
			else if (module->editingPpqn != 0ul) {
				snprintf(displayStr, 6, "  x%2u", (unsigned) module->sek[0].getPulsesPerStep());
			}
			else if (module->displayState == PhraseSeq32Ex::DISP_MODE) {
				if (editingSequence)
					runModeToStr(module->sek[0].getRunModeSeq(module->sequence));
				else
					runModeToStr(module->sek[0].getRunModeSong());
			}
			else if (module->displayState == PhraseSeq32Ex::DISP_LENGTH) {
				if (editingSequence)
					snprintf(displayStr, 6, "  L%2u", (unsigned) module->sek[0].getLength(module->sequence));
				else
					snprintf(displayStr, 6, "  L%2u", (unsigned) module->sek[0].getPhrases());
			}
			else if (module->displayState == PhraseSeq32Ex::DISP_TRANSPOSE) {
				int tranOffset = module->sek[0].getTransposeOffset(module->sequence);
				snprintf(displayStr, 6, "  +%2u", (unsigned) abs(tranOffset));
				if (tranOffset < 0)
					displayStr[2] = '-';
			}
			else if (module->displayState == PhraseSeq32Ex::DISP_ROTATE) {
				snprintf(displayStr, 6, "  )%2u", (unsigned) abs(module->rotateOffset));
				if (module->rotateOffset < 0)
					displayStr[2] = '(';
			}
			else if (module->displayState == PhraseSeq32Ex::DISP_REPS) {
				snprintf(displayStr, 6, "  R%2u", (unsigned) abs(module->sek[0].getPhraseReps(module->phraseIndexEdit)));
			}
			else {// DISP_NORMAL
				if (editingSequence)
					snprintf(displayStr, 6, "   %2u", (unsigned)(module->sequence + 1) );
				else
					snprintf(displayStr, 6, "%2u %2u", (unsigned)(module->phraseIndexEdit + 1), (unsigned)(module->sek[0].getPhrase(module->phraseIndexEdit) + 1) );
			}
		}
	};
	
	
	struct PanelThemeItem : MenuItem {
		PhraseSeq32Ex *module;
		int theme;
		void onAction(EventAction &e) override {
			module->panelTheme = theme;
		}
		void step() override {
			rightText = (module->panelTheme == theme) ? "✔" : "";
		}
	};
	struct ExpansionItem : MenuItem {
		PhraseSeq32Ex *module;
		void onAction(EventAction &e) override {
			module->expansion = module->expansion == 1 ? 0 : 1;
		}
	};
	struct ResetOnRunItem : MenuItem {
		PhraseSeq32Ex *module;
		void onAction(EventAction &e) override {
			module->resetOnRun = !module->resetOnRun;
		}
	};
	struct AutoseqItem : MenuItem {
		PhraseSeq32Ex *module;
		void onAction(EventAction &e) override {
			module->autoseq = !module->autoseq;
		}
	};
	Menu *createContextMenu() override {
		Menu *menu = ModuleWidget::createContextMenu();

		MenuLabel *spacerLabel = new MenuLabel();
		menu->addChild(spacerLabel);

		PhraseSeq32Ex *module = dynamic_cast<PhraseSeq32Ex*>(this->module);
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

		AutoseqItem *aseqItem = MenuItem::create<AutoseqItem>("AutoSeq when writing via CV inputs", CHECKMARK(module->autoseq));
		aseqItem->module = module;
		menu->addChild(aseqItem);

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
		
	PhraseSeq32ExWidget(PhraseSeq32Ex *module) : ModuleWidget(module) {
		this->module = module;
		oldExpansion = -1;
		
		// Main panel from Inkscape
        panel = new DynamicSVGPanel();
        panel->mode = &module->panelTheme;
		panel->expWidth = &expWidth;
        panel->addPanel(SVG::load(assetPlugin(plugin, "res/light/PhraseSeq32Ex.svg")));
        panel->addPanel(SVG::load(assetPlugin(plugin, "res/light/PhraseSeq32Ex.svg")));
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

		
		
		// ****** Top row ******
		
		static const int rowRulerT0 = 56;
		static const int columnRulerT0 = 25;// Step/Phase LED buttons
		static const int columnRulerT1 = 367;// AllSteps 
		static const int columnRulerT2 = 405;// Attach 
		static const int columnRulerT3 = 445;// Clk Res 
		static const int columnRulerT4 = 487;// Edit mode switch 
		static const int columnRulerT5 = columnRulerT4 + 52;// Play mode switch 

		// Step/Phrase LED buttons
		int posX = columnRulerT0;
		static int spacingSteps = 20;
		static int spacingSteps4 = 4;
		const int numX = SequencerKernel::MAX_SEQS / 2;
		for (int x = 0; x < numX; x++) {
			// First row
			addParam(createParamCentered<LEDButton>(Vec(posX, rowRulerT0 - 10), module, PhraseSeq32Ex::STEP_PHRASE_PARAMS + x, 0.0f, 1.0f, 0.0f));
			addChild(createLightCentered<MediumLight<GreenRedLight>>(Vec(posX, rowRulerT0 - 10), module, PhraseSeq32Ex::STEP_PHRASE_LIGHTS + (x * 2)));
			// Second row
			addParam(createParamCentered<LEDButton>(Vec(posX, rowRulerT0 + 10), module, PhraseSeq32Ex::STEP_PHRASE_PARAMS + x + numX, 0.0f, 1.0f, 0.0f));
			addChild(createLightCentered<MediumLight<GreenRedLight>>(Vec(posX, rowRulerT0 + 10), module, PhraseSeq32Ex::STEP_PHRASE_LIGHTS + ((x + numX) * 2)));
			// step position to next location and handle groups of four
			posX += spacingSteps;
			if ((x + 1) % 4 == 0)
				posX += spacingSteps4;
		}
		// AllSteps button
		addParam(createDynamicParamCentered<IMPushButton>(Vec(columnRulerT1, rowRulerT0), module, PhraseSeq32Ex::ALLSTEPS_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Attached LED button
		addParam(createParamCentered<LEDButton>(Vec(columnRulerT2, rowRulerT0), module, PhraseSeq32Ex::ATTACH_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(createLightCentered<MediumLight<RedLight>>(Vec(columnRulerT2, rowRulerT0), module, PhraseSeq32Ex::ATTACH_LIGHT));
		// Clk res
		addParam(createDynamicParamCentered<IMPushButton>(Vec(columnRulerT3, rowRulerT0), module, PhraseSeq32Ex::CLKRES_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Edit/play mode switches
		addParam(createParamCentered<CKSS>(Vec(columnRulerT4, rowRulerT0), module, PhraseSeq32Ex::EDIT_PARAM, 0.0f, 1.0f, 1.0f));
		addParam(createParamCentered<CKSS>(Vec(columnRulerT5, rowRulerT0), module, PhraseSeq32Ex::PLAY_PARAM, 0.0f, 1.0f, 1.0f));
		
		
		
		// ****** Octave and keyboard area ******
		
		// Octave LED buttons
		static const int octLightsIntY = 20;
		static const int rowRulerOct = 111;
		for (int i = 0; i < 7; i++) {
			addParam(createParamCentered<LEDButton>(Vec(columnRulerT0, rowRulerOct + i * octLightsIntY), module, PhraseSeq32Ex::OCTAVE_PARAM + i, 0.0f, 1.0f, 0.0f));
			addChild(createLightCentered<MediumLight<RedLight>>(Vec(columnRulerT0, rowRulerOct + i * octLightsIntY), module, PhraseSeq32Ex::OCTAVE_LIGHTS + i));
		}
		// Keys and Key lights
		static const int keyNudgeX = 2;
		static const int KeyBlackY = 103;
		static const int KeyWhiteY = 141;
		static const int offsetKeyLEDx = 6;
		static const int offsetKeyLEDy = 16;
		// Black keys and lights
		addParam(createParam<InvisibleKeySmall>(			Vec(65+keyNudgeX, KeyBlackY), module, PhraseSeq32Ex::KEY_PARAMS + 1, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(65+keyNudgeX+offsetKeyLEDx, KeyBlackY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 1 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(93+keyNudgeX, KeyBlackY), module, PhraseSeq32Ex::KEY_PARAMS + 3, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(93+keyNudgeX+offsetKeyLEDx, KeyBlackY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 3 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(150+keyNudgeX, KeyBlackY), module, PhraseSeq32Ex::KEY_PARAMS + 6, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(150+keyNudgeX+offsetKeyLEDx, KeyBlackY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 6 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(178+keyNudgeX, KeyBlackY), module, PhraseSeq32Ex::KEY_PARAMS + 8, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(178+keyNudgeX+offsetKeyLEDx, KeyBlackY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 8 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(206+keyNudgeX, KeyBlackY), module, PhraseSeq32Ex::KEY_PARAMS + 10, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(206+keyNudgeX+offsetKeyLEDx, KeyBlackY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 10 * 2));
		// White keys and lights
		addParam(createParam<InvisibleKeySmall>(			Vec(51+keyNudgeX, KeyWhiteY), module, PhraseSeq32Ex::KEY_PARAMS + 0, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(51+keyNudgeX+offsetKeyLEDx, KeyWhiteY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 0 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(79+keyNudgeX, KeyWhiteY), module, PhraseSeq32Ex::KEY_PARAMS + 2, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(79+keyNudgeX+offsetKeyLEDx, KeyWhiteY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 2 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(107+keyNudgeX, KeyWhiteY), module, PhraseSeq32Ex::KEY_PARAMS + 4, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(107+keyNudgeX+offsetKeyLEDx, KeyWhiteY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 4 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(136+keyNudgeX, KeyWhiteY), module, PhraseSeq32Ex::KEY_PARAMS + 5, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(136+keyNudgeX+offsetKeyLEDx, KeyWhiteY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 5 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(164+keyNudgeX, KeyWhiteY), module, PhraseSeq32Ex::KEY_PARAMS + 7, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(164+keyNudgeX+offsetKeyLEDx, KeyWhiteY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 7 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(192+keyNudgeX, KeyWhiteY), module, PhraseSeq32Ex::KEY_PARAMS + 9, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(192+keyNudgeX+offsetKeyLEDx, KeyWhiteY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 9 * 2));
		addParam(createParam<InvisibleKeySmall>(			Vec(220+keyNudgeX, KeyWhiteY), module, PhraseSeq32Ex::KEY_PARAMS + 11, 0.0, 1.0, 0.0));
		addChild(createLight<MediumLight<GreenRedLight>>(Vec(220+keyNudgeX+offsetKeyLEDx, KeyWhiteY+offsetKeyLEDy), module, PhraseSeq32Ex::KEY_LIGHTS + 11 * 2));

		
		
		// ****** Gate and slide section ******
		
		static const int rowRulerMB0 = rowRulerOct + 6 * octLightsIntY;
		static const int columnRulerMB1 = 67;// Gate 
		static const int columnRulerMBspacing = 70;
		static const int columnRulerMB2 = columnRulerMB1 + columnRulerMBspacing;// Tie
		static const int columnRulerMB3 = columnRulerMB2 + columnRulerMBspacing;// GateP
		static const int columnRulerMB4 = columnRulerMB3 + columnRulerMBspacing;// Slide
		static const int posLEDvsButton = + 26;
		
		// Gate 1 light and button
		addChild(createLightCentered<MediumLight<GreenRedLight>>(Vec(columnRulerMB1 + posLEDvsButton, rowRulerMB0), module, PhraseSeq32Ex::GATE_LIGHT));		
		addParam(createDynamicParamCentered<IMBigPushButton>(Vec(columnRulerMB1, rowRulerMB0), module, PhraseSeq32Ex::GATE_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Gate 1 probability light and button
		addChild(createLightCentered<MediumLight<RedLight>>(Vec(columnRulerMB2 + posLEDvsButton, rowRulerMB0), module, PhraseSeq32Ex::GATE_PROB_LIGHT));		
		addParam(createDynamicParamCentered<IMBigPushButton>(Vec(columnRulerMB2, rowRulerMB0), module, PhraseSeq32Ex::GATE_PROB_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Tie light and button
		addChild(createLightCentered<MediumLight<RedLight>>(Vec(columnRulerMB3 + posLEDvsButton, rowRulerMB0), module, PhraseSeq32Ex::TIE_LIGHT));		
		addParam(createDynamicParamCentered<IMBigPushButton>(Vec(columnRulerMB3, rowRulerMB0), module, PhraseSeq32Ex::TIE_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Slide light and button
		addChild(createLightCentered<MediumLight<RedLight>>(Vec(columnRulerMB4 + posLEDvsButton, rowRulerMB0), module, PhraseSeq32Ex::SLIDE_LIGHT));		
		addParam(createDynamicParamCentered<IMBigPushButton>(Vec(columnRulerMB4, rowRulerMB0), module, PhraseSeq32Ex::SLIDE_BTN_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));

	
	
		// ****** Right side control area ******
		
		static const int rowRulerDisp = 110;
		static const int rowRulerKnobs = 145;
		static const int rowRulerSmallButtons = 189;
		static const int displayHeights = 24;

		// Velocity display
		static const int colRulerKM = 285;
		addChild(new VelocityDisplayWidget(Vec(colRulerKM, rowRulerDisp), Vec(46, displayHeights), module));// 3 characters
		// Velocity knob
		addParam(createDynamicParamCentered<IMMediumKnobInf>(Vec(colRulerKM, rowRulerKnobs), module, PhraseSeq32Ex::VEL_KNOB_PARAM, -INFINITY, INFINITY, 0.0f, &module->panelTheme));	
		// Key mode LED buttons	
		addParam(createParamCentered<LEDButton>(Vec(colRulerKM - 13, rowRulerSmallButtons), module, PhraseSeq32Ex::KEYNOTE_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(createLightCentered<MediumLight<RedLight>>(Vec(colRulerKM - 13, rowRulerSmallButtons), module, PhraseSeq32Ex::KEYNOTE_LIGHT));
		addParam(createParamCentered<LEDButton>(Vec(colRulerKM + 13, rowRulerSmallButtons), module, PhraseSeq32Ex::KEYGATE_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(createLightCentered<MediumLight<GreenRedLight>>(Vec(colRulerKM + 13, rowRulerSmallButtons), module, PhraseSeq32Ex::KEYGATE_LIGHT));
		
		// Track display
		static const int colRulerTrk = 343;
		addChild(new TrackDisplayWidget(Vec(colRulerTrk, rowRulerDisp), Vec(32, displayHeights), module));// 2 characters
		// Track knob
		addParam(createDynamicParamCentered<IMMediumKnobInf>(Vec(colRulerTrk, rowRulerKnobs), module, PhraseSeq32Ex::TRACK_PARAM, -INFINITY, INFINITY, 0.0f, &module->panelTheme));	
		// AllTracks button
		addParam(createDynamicParamCentered<IMPushButton>(Vec(colRulerTrk, rowRulerSmallButtons), module, PhraseSeq32Ex::ALLTRACKS_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Transpose/rotate button
		addParam(createDynamicParamCentered<IMBigPushButton>(Vec(colRulerTrk, rowRulerMB0), module, PhraseSeq32Ex::TRAN_ROT_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		

		// PhraseSeq edit display 
		static const int colRulerEditPS = 413;
		addChild(new PSEditDisplayWidget(Vec(colRulerEditPS, rowRulerDisp), Vec(71, displayHeights), module));// 5 characters
		// Sequence-edit knob
		static const int psKnobOffsetX = 22;
		addParam(createDynamicParamCentered<IMMediumKnobInf>(Vec(colRulerEditPS + psKnobOffsetX, rowRulerKnobs), module, PhraseSeq32Ex::SEQUENCE_PARAM, -INFINITY, INFINITY, 0.0f, &module->panelTheme));		
		// Phrase knob
		addParam(createDynamicParamCentered<IMMediumKnobInf>(Vec(colRulerEditPS - psKnobOffsetX, rowRulerKnobs), module, PhraseSeq32Ex::PHRASE_PARAM, -INFINITY, INFINITY, 0.0f, &module->panelTheme));		
		// Reps button
		addParam(createDynamicParamCentered<IMPushButton>(Vec(colRulerEditPS - psKnobOffsetX, rowRulerSmallButtons), module, PhraseSeq32Ex::REPS_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// AllSeqs button
		addParam(createDynamicParamCentered<IMPushButton>(Vec(colRulerEditPS + psKnobOffsetX, rowRulerSmallButtons), module, PhraseSeq32Ex::ALLSEQS_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Len/mode button
		addParam(createDynamicParamCentered<IMBigPushButton>(Vec(colRulerEditPS, rowRulerMB0), module, PhraseSeq32Ex::LENMODE_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));

			
		// PhraseSeq play display 
		static const int colRulerPlayPS = 510;
		addChild(new PSPlayDisplayWidget(Vec(colRulerPlayPS, rowRulerDisp), Vec(71, displayHeights), module));// 5 characters
		// Sequence-play knob
		addParam(createDynamicParamCentered<IMMediumKnobInf>(Vec(colRulerPlayPS + psKnobOffsetX, rowRulerKnobs), module, PhraseSeq32Ex::SEQUENCE_PLAY_PARAM, -INFINITY, INFINITY, 0.0f, &module->panelTheme));		
		// Commit button
		addParam(createDynamicParamCentered<IMPushButton>(Vec(colRulerPlayPS - psKnobOffsetX, rowRulerKnobs), module, PhraseSeq32Ex::COMMIT_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Run LED bezel and light
		static const int runResetOffsetX = psKnobOffsetX;
		addParam(createParamCentered<LEDBezel>(Vec(colRulerPlayPS - runResetOffsetX, rowRulerSmallButtons), module, PhraseSeq32Ex::RUN_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(createLightCentered<MuteLight<GreenLight>>(Vec(colRulerPlayPS - psKnobOffsetX, rowRulerSmallButtons), module, PhraseSeq32Ex::RUN_LIGHT));
		// Reset LED bezel and light
		addParam(createParamCentered<LEDBezel>(Vec(colRulerPlayPS + runResetOffsetX, rowRulerSmallButtons), module, PhraseSeq32Ex::RESET_PARAM, 0.0f, 1.0f, 0.0f));
		addChild(createLightCentered<MuteLight<GreenLight>>(Vec(colRulerPlayPS + psKnobOffsetX, rowRulerSmallButtons), module, PhraseSeq32Ex::RESET_LIGHT));


		// Copy/paste buttons
		addParam(createDynamicParamCentered<IMPushButton>(Vec(475 - 10, rowRulerMB0 + 3), module, PhraseSeq32Ex::COPY_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		addParam(createDynamicParamCentered<IMPushButton>(Vec(475 + 20, rowRulerMB0 + 3), module, PhraseSeq32Ex::PASTE_PARAM, 0.0f, 1.0f, 0.0f, &module->panelTheme));
		// Copy-paste mode switch (3 position)
		addParam(createParamCentered<CKSSThreeInv>(Vec(543, rowRulerMB0 - 5 + 3), module, PhraseSeq32Ex::CPMODE_PARAM, 0.0f, 2.0f, 2.0f));	// 0.0f is top position

		
		
						
		
		// ****** Bottom two rows ******
		
		static const int rowRulerBLow = 335;
		static const int rowRulerBHigh = 286;
		
		static const int inputJackSpacingX = 50;
		static const int columnRulerB0 = 28;
		static const int columnRulerB1 = columnRulerB0 + inputJackSpacingX;
		static const int columnRulerB2 = columnRulerB1 + inputJackSpacingX;
		static const int columnRulerB3 = columnRulerB2 + inputJackSpacingX;
		static const int columnRulerB4 = columnRulerB3 + inputJackSpacingX;
		static const int columnRulerB5 = columnRulerB4 + inputJackSpacingX;// clock and reset
		
		static const int outputJackSpacingX = 42;
		static const int columnRulerB6 = columnRulerB5 + inputJackSpacingX - 2;// outputs
		static const int columnRulerB7 = columnRulerB6 + outputJackSpacingX;// outputs
		static const int columnRulerB8 = columnRulerB7 + outputJackSpacingX;// outputs
		static const int columnRulerB9 = columnRulerB8 + outputJackSpacingX + 3;// outputs
		static const int columnRulerB10 = columnRulerB9 + outputJackSpacingX;// outputs
		static const int columnRulerB11 = columnRulerB10 + outputJackSpacingX;// outputs
		
		// Step arrow CV inputs
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB0, rowRulerBHigh), Port::INPUT, module, PhraseSeq32Ex::LEFTCV_INPUT, &module->panelTheme));
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB0, rowRulerBLow), Port::INPUT, module, PhraseSeq32Ex::RIGHTCV_INPUT, &module->panelTheme));

		// Autostep and write
		addParam(createParamCentered<CKSS>(Vec(columnRulerB1, rowRulerBHigh), module, PhraseSeq32Ex::AUTOSTEP_PARAM, 0.0f, 1.0f, 1.0f));		
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB1, rowRulerBLow), Port::INPUT, module, PhraseSeq32Ex::WRITE_INPUT, &module->panelTheme));
	
		// CV IN inputs
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB2, rowRulerBHigh), Port::INPUT, module, PhraseSeq32Ex::CV_INPUTS + 0, &module->panelTheme));
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB3, rowRulerBHigh), Port::INPUT, module, PhraseSeq32Ex::CV_INPUTS + 1, &module->panelTheme));
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB2, rowRulerBLow), Port::INPUT, module, PhraseSeq32Ex::CV_INPUTS + 3, &module->panelTheme));
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB3, rowRulerBLow), Port::INPUT, module, PhraseSeq32Ex::CV_INPUTS + 4, &module->panelTheme));
		
		// Velocity input and run CV input
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB4, rowRulerBHigh), Port::INPUT, module, PhraseSeq32Ex::VEL_INPUT, &module->panelTheme));
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB4, rowRulerBLow), Port::INPUT, module, PhraseSeq32Ex::RUNCV_INPUT, &module->panelTheme));
		
		
		// Clock and reset inputs
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB5, rowRulerBHigh), Port::INPUT, module, PhraseSeq32Ex::CLOCK_INPUT, &module->panelTheme));
		addInput(createDynamicPortCentered<IMPort>(Vec(columnRulerB5, rowRulerBLow), Port::INPUT, module, PhraseSeq32Ex::RESET_INPUT, &module->panelTheme));

		// CV+Gate+Vel outputs
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB6, rowRulerBHigh), Port::OUTPUT, module, PhraseSeq32Ex::CV_OUTPUTS + 0, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB7, rowRulerBHigh), Port::OUTPUT, module, PhraseSeq32Ex::GATE_OUTPUTS + 0, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB8, rowRulerBHigh), Port::OUTPUT, module, PhraseSeq32Ex::VEL_OUTPUTS + 0, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB9, rowRulerBHigh), Port::OUTPUT, module, PhraseSeq32Ex::CV_OUTPUTS + 1, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB10, rowRulerBHigh), Port::OUTPUT, module, PhraseSeq32Ex::GATE_OUTPUTS + 1, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB11, rowRulerBHigh), Port::OUTPUT, module, PhraseSeq32Ex::VEL_OUTPUTS + 1, &module->panelTheme));
		//
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB6, rowRulerBLow), Port::OUTPUT, module, PhraseSeq32Ex::CV_OUTPUTS + 2, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB7, rowRulerBLow), Port::OUTPUT, module, PhraseSeq32Ex::GATE_OUTPUTS + 2, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB8, rowRulerBLow), Port::OUTPUT, module, PhraseSeq32Ex::VEL_OUTPUTS + 2, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB9, rowRulerBLow), Port::OUTPUT, module, PhraseSeq32Ex::CV_OUTPUTS + 3, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB10, rowRulerBLow), Port::OUTPUT, module, PhraseSeq32Ex::GATE_OUTPUTS + 3, &module->panelTheme));
		addOutput(createDynamicPortCentered<IMPort>(Vec(columnRulerB11, rowRulerBLow), Port::OUTPUT, module, PhraseSeq32Ex::VEL_OUTPUTS + 3, &module->panelTheme));


		
		
		// Expansion module
		static const int rowRulerExpTop = 78;
		static const int rowSpacingExp = 60;
		static const int colRulerExp = panel->box.size.x - expWidth / 2;
		addInput(expPorts[0] = createDynamicPortCentered<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 0), Port::INPUT, module, PhraseSeq32Ex::GATECV_INPUT, &module->panelTheme));
		addInput(expPorts[1] = createDynamicPortCentered<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 1), Port::INPUT, module, PhraseSeq32Ex::GATEPCV_INPUT, &module->panelTheme));
		addInput(expPorts[2] = createDynamicPortCentered<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 2), Port::INPUT, module, PhraseSeq32Ex::TIEDCV_INPUT, &module->panelTheme));
		addInput(expPorts[3] = createDynamicPortCentered<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 3), Port::INPUT, module, PhraseSeq32Ex::SLIDECV_INPUT, &module->panelTheme));
		addInput(expPorts[4] = createDynamicPortCentered<IMPort>(Vec(colRulerExp, rowRulerExpTop + rowSpacingExp * 4), Port::INPUT, module, PhraseSeq32Ex::SEQCV_INPUT, &module->panelTheme));
	}
};

Model *modelPhraseSeq32Ex = Model::create<PhraseSeq32Ex, PhraseSeq32ExWidget>("Impromptu Modular", "Phrase-Seq-32Ex", "SEQ - Phrase-Seq-32Ex", SEQUENCER_TAG);

/*CHANGE LOG

0.6.13:
created

*/
