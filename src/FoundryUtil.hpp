//***********************************************************************************************
//Impromptu Modular: Modules for VCV Rack by Marc Boulé
//***********************************************************************************************

#include "ImpromptuModular.hpp"
#include "dsp/digital.hpp"

using namespace rack;


class StepAttributes {
	unsigned long attributes;
	
	public:

	static const unsigned long ATT_MSK_GATE =      0x01000000, gateShift = 24;
	static const unsigned long ATT_MSK_GATEP =     0x02000000;
	static const unsigned long ATT_MSK_SLIDE =     0x04000000;
	static const unsigned long ATT_MSK_TIED =      0x08000000;
	static const unsigned long ATT_MSK_GATETYPE =  0xF0000000, gateTypeShift = 28;
	static const unsigned long ATT_MSK_VELOCITY =  0x000000FF, velocityShift = 0;
	static const unsigned long ATT_MSK_GATEP_VAL = 0x0000FF00, gatePValShift = 8;
	static const unsigned long ATT_MSK_SLIDE_VAL = 0x00FF0000, slideValShift = 16;

	static const int INIT_VELOCITY = 100;
	static const int MAX_VELOCITY = 200;
	static const int INIT_PROB = 50;// range is 0 to 100
	static const int INIT_SLIDE = 10;// range is 0 to 100
	
	static const unsigned long ATT_MSK_INITSTATE = ((ATT_MSK_GATE) | (INIT_VELOCITY << velocityShift) | (INIT_PROB << gatePValShift) | (INIT_SLIDE << slideValShift));

	inline void clear() {attributes = 0ul;}
	inline void init() {attributes = ATT_MSK_INITSTATE;}
	inline void randomize() {attributes = ( (randomu32() & (ATT_MSK_GATE | ATT_MSK_GATEP | ATT_MSK_SLIDE | ATT_MSK_TIED)) | ((randomu32() % 101) << gatePValShift) | ((randomu32() % 101) << slideValShift) | (randomu32() % (MAX_VELOCITY + 1)) ) ;}
	
	inline bool getGate() {return (attributes & ATT_MSK_GATE) != 0;}
	inline int getGateType() {return (int)((attributes & ATT_MSK_GATETYPE) >> gateTypeShift);}
	inline bool getTied() {return (attributes & ATT_MSK_TIED) != 0;}
	inline bool getGateP() {return (attributes & ATT_MSK_GATEP) != 0;}
	inline int getGatePVal() {return (int)((attributes & ATT_MSK_GATEP_VAL) >> gatePValShift);}
	inline bool getSlide() {return (attributes & ATT_MSK_SLIDE) != 0;}
	inline int getSlideVal() {return (int)((attributes & ATT_MSK_SLIDE_VAL) >> slideValShift);}
	inline int getVelocityVal() {return (int)((attributes & ATT_MSK_VELOCITY) >> velocityShift);}
	inline unsigned long getAttribute() {return attributes;}

	inline void setGate(bool gate1State) {attributes &= ~ATT_MSK_GATE; if (gate1State) attributes |= ATT_MSK_GATE;}
	inline void setGateType(int gateType) {attributes &= ~ATT_MSK_GATETYPE; attributes |= (((unsigned long)gateType) << gateTypeShift);}
	inline void setTied(bool tiedState) {
		attributes &= ~ATT_MSK_TIED; 
		if (tiedState) {
			attributes |= ATT_MSK_TIED;
			attributes &= ~(ATT_MSK_GATE | ATT_MSK_GATEP | ATT_MSK_SLIDE);// clear other attributes if tied
		}
	}
	inline void setGateP(bool GatePState) {attributes &= ~ATT_MSK_GATEP; if (GatePState) attributes |= ATT_MSK_GATEP;}
	inline void setGatePVal(int gatePval) {attributes &= ~ATT_MSK_GATEP_VAL; attributes |= (((unsigned long)gatePval) << gatePValShift);}
	inline void setSlide(bool slideState) {attributes &= ~ATT_MSK_SLIDE; if (slideState) attributes |= ATT_MSK_SLIDE;}
	inline void setSlideVal(int slideVal) {attributes &= ~ATT_MSK_SLIDE_VAL; attributes |= (((unsigned long)slideVal) << slideValShift);}
	inline void setVelocityVal(int _velocity) {attributes &= ~ATT_MSK_VELOCITY; attributes |= (((unsigned long)_velocity) << velocityShift);}
	inline void setAttribute(unsigned long _attributes) {attributes = _attributes;}
};// class StepAttributes


//*****************************************************************************


class Phrase {
	// a phrase is a sequence number and a number of repetitions; it is used to make a song
	unsigned long phrase;
	
	public:

	static const unsigned long PHR_MSK_SEQNUM = 0x00FF;
	static const unsigned long PHR_MSK_REPS =   0xFF00, repShift = 8;// a rep is 0 to 99
	
	inline void init() {phrase = (1 << repShift);}
	inline void randomize(int maxSeqs) {phrase = ((randomu32() % maxSeqs) | ((randomu32() % 4 + 1) << repShift));}
	
	inline int getSeqNum() {return (int)(phrase & PHR_MSK_SEQNUM);}
	inline int getReps() {return (int)((phrase & PHR_MSK_REPS) >> repShift);}
	inline unsigned long getPhraseJson() {return phrase - (1 << repShift);}// compression trick (store 0 instead of 1)
	
	inline void setSeqNum(int seqn) {phrase &= ~PHR_MSK_SEQNUM; phrase |= ((unsigned long)seqn);}
	inline void setReps(int _reps) {phrase &= ~PHR_MSK_REPS; phrase |= (((unsigned long)_reps) << repShift);}
	inline void setPhraseJson(unsigned long _phrase) {phrase = (_phrase + (1 << repShift));}// compression trick (store 0 instead of 1)
};// class Phrase


//*****************************************************************************


class SeqAttributes {
	unsigned long attributes;
	
	public:

	static const unsigned long SEQ_MSK_LENGTH  =   0x000000FF;// number of steps in each sequence, min value is 1
	static const unsigned long SEQ_MSK_RUNMODE =   0x0000FF00, runModeShift = 8;
	static const unsigned long SEQ_MSK_TRANSPOSE = 0x007F0000, transposeShift = 16;
	static const unsigned long SEQ_MSK_TRANSIGN =  0x00800000;// manually implement sign bit
	static const unsigned long SEQ_MSK_ROTATE =    0x7F000000, rotateShift = 24;
	static const unsigned long SEQ_MSK_ROTSIGN =   0x80000000;// manually implement sign bit (+ is right, - is left)
	
	inline void init(int length, int runMode) {attributes = ((length) | (((unsigned long)runMode) << runModeShift));}
	inline void randomize(int maxSteps, int numModes) {attributes = ( (1 + (randomu32() % maxSteps)) | (((unsigned long)(randomu32() % numModes) << runModeShift)) );}
	
	inline int getLength() {return (int)(attributes & SEQ_MSK_LENGTH);}
	inline int getRunMode() {return (int)((attributes & SEQ_MSK_RUNMODE) >> runModeShift);}
	inline int getTranspose() {
		int ret = (int)((attributes & SEQ_MSK_TRANSPOSE) >> transposeShift);
		if ( (attributes & SEQ_MSK_TRANSIGN) != 0)// if negative
			ret *= -1;
		return ret;
	}
	inline int getRotate() {
		int ret = (int)((attributes & SEQ_MSK_ROTATE) >> rotateShift);
		if ( (attributes & SEQ_MSK_ROTSIGN) != 0)// if negative
			ret *= -1;
		return ret;
	}
	inline unsigned long getSeqAttrib() {return attributes;}
	
	inline void setLength(int length) {attributes &= ~SEQ_MSK_LENGTH; attributes |= ((unsigned long)length);}
	inline void setRunMode(int runMode) {attributes &= ~SEQ_MSK_RUNMODE; attributes |= (((unsigned long)runMode) << runModeShift);}
	inline void setTranspose(int transp) {
		attributes &= ~ (SEQ_MSK_TRANSPOSE | SEQ_MSK_TRANSIGN); 
		attributes |= (((unsigned long)abs(transp)) << transposeShift);
		if (transp < 0) 
			attributes |= SEQ_MSK_TRANSIGN;
	}
	inline void setRotate(int rotn) {
		attributes &= ~ (SEQ_MSK_ROTATE | SEQ_MSK_ROTSIGN); 
		attributes |= (((unsigned long)abs(rotn)) << rotateShift);
		if (rotn < 0) 
			attributes |= SEQ_MSK_ROTSIGN;
	}
	inline void setSeqAttrib(unsigned long _attributes) {attributes = _attributes;}
};// class SeqAttributes


//*****************************************************************************
// SequencerKernel
//*****************************************************************************


struct SeqCPbuffer;
struct SongCPbuffer;

class SequencerKernel {
	public: 
	
	
	// General constants
	// ----------------

	// Sequencer kernel dimensions
	static const int MAX_STEPS = 32;// must be a power of two (some multi select loops have bitwise "& (MAX_STEPS - 1)")
	static const int MAX_SEQS = 64;
	static const int MAX_PHRASES = 99;// maximum value is 99 (index value is 0 to 98; disp will be 1 to 99)

	// Run modes
	enum RunModeIds {MODE_FWD, MODE_REV, MODE_PPG, MODE_PEN, MODE_BRN, MODE_RND, MODE_TKA, NUM_MODES};
	static const std::string modeLabels[NUM_MODES];
	
	// Gate types
	static const int NUM_GATES = 12;	
	static const uint64_t advGateHitMaskLow[NUM_GATES];		
	static const uint64_t advGateHitMaskHigh[NUM_GATES];

	static constexpr float INIT_CV = 0.0f;


	private:
	

	// Member data
	// ----------------	
	int id;
	std::string ids;
	
	// Need to save
	int pulsesPerStep;// stored range is [1:49] so must ALWAYS read thgouth getPulsesPerStep(). Must do this because of knob
	int delay;
	int runModeSong;	
	int songBeginIndex;
	int songEndIndex;
	Phrase phrases[MAX_PHRASES];// This is the song (series of phases; a phrase is a sequence number and a repetition value)	
	SeqAttributes sequences[MAX_SEQS];
	float cv[MAX_SEQS][MAX_STEPS];// [-3.0 : 3.917].
	StepAttributes attributes[MAX_SEQS][MAX_STEPS];
	
	// No need to save
	int stepIndexRun;
	unsigned long stepIndexRunHistory;
	int phraseIndexRun;
	unsigned long phraseIndexRunHistory;
	int ppqnCount;
	int ppqnLeftToSkip;// used in clock delay
	int gateCode;// -1 = Killed for all pulses of step, 0 = Low for current pulse of step, 1 = High for current pulse of step, 2 = Clk high pulse, 3 = 1ms trig
	unsigned long slideStepsRemain;// 0 when no slide under way, downward step counter when sliding
	float slideCVdelta;// no need to initialize, this is only used when slideStepsRemain is not 0
	SequencerKernel *masterKernel;// nullprt for track 0, used for grouped run modes (tracks B,C,D follow A when random, for example)
	bool* holdTiedNotesPtr;
	unsigned long clockPeriod;// counts number of step() calls upward from last clock (reset after clock processed)
	
	
	public: 
	
	
	void construct(int _id, SequencerKernel *_masterKernel, bool* _holdTiedNotesPtr); // don't want regaular constructor mechanism
	
	
	inline int getRunModeSong() {return runModeSong;}
	inline int getRunModeSeq(int seqn) {return sequences[seqn].getRunMode();}
	inline int getBegin() {return songBeginIndex;}
	inline int getEnd() {return songEndIndex;}
	inline int getLength(int seqn) {return sequences[seqn].getLength();}
	inline int getPhraseSeq(int phrn) {return phrases[phrn].getSeqNum();}
	inline int getPhraseReps(int phrn) {return phrases[phrn].getReps();}
	inline int getPulsesPerStep() {return (pulsesPerStep > 2 ? ((pulsesPerStep - 1) << 1) : pulsesPerStep);}
	inline int getDelay() {return delay;}
	inline int getTransposeOffset(int seqn) {return sequences[seqn].getTranspose();}
	inline int getRotateOffset(int seqn) {return sequences[seqn].getRotate();}
	inline int getStepIndexRun() {return stepIndexRun;}
	inline int getPhraseIndexRun() {return phraseIndexRun;}
	inline float getCV(int seqn, int stepn) {return cv[seqn][stepn];}
	inline float getCVRun() {return cv[phrases[phraseIndexRun].getSeqNum()][stepIndexRun];}
	inline StepAttributes getAttribute(int seqn, int stepn) {return attributes[seqn][stepn];}
	inline StepAttributes getAttributeRun() {return attributes[phrases[phraseIndexRun].getSeqNum()][stepIndexRun];}
	inline bool getGate(int seqn, int stepn) {return attributes[seqn][stepn].getGate();}
	inline bool getGateP(int seqn, int stepn) {return attributes[seqn][stepn].getGateP();}
	inline bool getSlide(int seqn, int stepn) {return attributes[seqn][stepn].getSlide();}
	inline bool getTied(int seqn, int stepn) {return attributes[seqn][stepn].getTied();}
	inline int getGatePVal(int seqn, int stepn) {return attributes[seqn][stepn].getGatePVal();}
	inline int getSlideVal(int seqn, int stepn) {return attributes[seqn][stepn].getSlideVal();}
	inline int getVelocityVal(int seqn, int stepn) {return attributes[seqn][stepn].getVelocityVal();}
	inline int getVelocityValRun() {return getAttributeRun().getVelocityVal();}
	inline int getGateType(int seqn, int stepn) {return attributes[seqn][stepn].getGateType();}	
	
	inline void setPulsesPerStep(int _pps) {pulsesPerStep = _pps;}
	inline void setDelay(int _delay) {delay = _delay;}
	inline void setLength(int seqn, int _length) {sequences[seqn].setLength(_length);}
	inline void setPhraseReps(int phrn, int _reps) {phrases[phrn].setReps(_reps);}
	inline void setPhraseSeqNum(int phrn, int _seqn) {phrases[phrn].setSeqNum(_seqn);}
	inline void setBegin(int phrn) {songBeginIndex = phrn; songEndIndex = max(phrn, songEndIndex);}
	inline void setEnd(int phrn) {songEndIndex = phrn; songBeginIndex = min(phrn, songBeginIndex);}
	inline void setRunModeSong(int _runMode) {runModeSong = _runMode;}
	inline void setRunModeSeq(int seqn, int _runMode) {sequences[seqn].setRunMode(_runMode);}
	void setGate(int seqn, int stepn, bool newGate, int count);
	void setGateP(int seqn, int stepn, bool newGateP, int count);
	void setSlide(int seqn, int stepn, bool newSlide, int count);
	void setTied(int seqn, int stepn, bool newTied, int count);
	void setGatePVal(int seqn, int stepn, int gatePval, int count);
	void setSlideVal(int seqn, int stepn, int slideVal, int count);
	void setVelocityVal(int seqn, int stepn, int velocity, int count);
	void setGateType(int seqn, int stepn, int gateType, int count);
	
	
	inline int modRunModeSong(int delta) {
		runModeSong = clamp(runModeSong += delta, 0, NUM_MODES - 1);
		return runModeSong;
	}
	inline int modRunModeSeq(int seqn, int delta) {
		int rVal = sequences[seqn].getRunMode();
		rVal = clamp(rVal + delta, 0, NUM_MODES - 1);
		sequences[seqn].setRunMode(rVal);
		return rVal;
	}
	inline int modLength(int seqn, int delta) {
		int lVal = sequences[seqn].getLength();
		lVal = clamp(lVal + delta, 1, MAX_STEPS);
		sequences[seqn].setLength(lVal);
		return lVal;
	}
	inline int modPhraseSeqNum(int phrn, int delta) {
		int seqn = phrases[phrn].getSeqNum();
		seqn = moveIndex(seqn, seqn + delta, MAX_SEQS);
		phrases[phrn].setSeqNum(seqn);
		return seqn;
	}
	inline int modPhraseReps(int phrn, int delta) {
		int rVal = phrases[phrn].getReps();
		rVal = clamp(rVal + delta, 0, 99);
		phrases[phrn].setReps(rVal);
		return rVal;
	}		
	inline int modPulsesPerStep(int delta) {
		pulsesPerStep += delta;
		if (pulsesPerStep < 1) pulsesPerStep = 1;
		if (pulsesPerStep > 49) pulsesPerStep = 49;
		return pulsesPerStep;
	}
	inline int modDelay(int delta) {
		delay = clamp(delay + delta, 0, 99);
		return delay;
	}
	inline int modGatePVal(int seqn, int stepn, int delta, int count) {
		int pVal = getGatePVal(seqn, stepn);
		pVal = clamp(pVal + delta, 0, 100);
		setGatePVal(seqn, stepn, pVal, count);
		return pVal;
	}		
	inline int modSlideVal(int seqn, int stepn, int delta, int count) {
		int sVal = getSlideVal(seqn, stepn);
		sVal = clamp(sVal + delta, 0, 100);
		setSlideVal(seqn, stepn, sVal, count);
		return sVal;
	}		
	inline int modVelocityVal(int seqn, int stepn, int delta, int upperLimit, int count) {
		int vVal = getVelocityVal(seqn, stepn);
		vVal = clamp(vVal + delta, 0, upperLimit);
		setVelocityVal(seqn, stepn, vVal, count);
		return vVal;
	}		
	inline void decSlideStepsRemain() {if (slideStepsRemain > 0ul) slideStepsRemain--;}	
	inline bool toggleGate(int seqn, int stepn, int count) {
		bool newGate = !attributes[seqn][stepn].getGate();
		setGate(seqn, stepn, newGate, count);
		return newGate;
	}
	inline bool toggleGateP(int seqn, int stepn, int count) {
		bool newGateP = !attributes[seqn][stepn].getGateP();
		setGateP(seqn, stepn, newGateP, count);
		return newGateP;
	}
	inline bool toggleSlide(int seqn, int stepn, int count) {
		bool newSlide = !attributes[seqn][stepn].getSlide();
		setSlide(seqn, stepn, newSlide, count);
		return newSlide;
	}	
	inline bool toggleTied(int seqn, int stepn, int count) {
		bool newTied = !attributes[seqn][stepn].getTied();
		setTied(seqn, stepn, newTied, count);
		return newTied;
	}	
	float applyNewOctave(int seqn, int stepn, int newOct, int count);
	float applyNewKey(int seqn, int stepn, int newKeyIndex, int count);
	void writeCV(int seqn, int stepn, float newCV, int count);
	
	
	inline float calcSlideOffset() {return (slideStepsRemain > 0ul ? (slideCVdelta * (float)slideStepsRemain) : 0.0f);}
	inline bool calcGate(SchmittTrigger clockTrigger, float sampleRate) {
		if (ppqnLeftToSkip != 0)
			return false;
		if (gateCode < 2) 
			return gateCode == 1;
		if (gateCode == 2)
			return clockTrigger.isHigh();
		return clockPeriod < (unsigned long) (sampleRate * 0.01f);
	}
	
	inline void initPulsesPerStep() {pulsesPerStep = 1;}
	inline void initDelay() {delay = 0;}
	
	void initSequence(int seqn);
	void initSong();
	void randomizeSequence(int seqn);
	void randomizeSong();	

	void copySequence(SeqCPbuffer* seqCPbuf, int seqn, int startCP, int countCP);
	void pasteSequence(SeqCPbuffer* seqCPbuf, int seqn, int startCP);
	void copySong(SongCPbuffer* songCPbuf, int startCP, int countCP);
	void pasteSong(SongCPbuffer* songCPbuf, int startCP);
	
	
	// Main methods
	// ----------------
		
	
	void reset();
	void randomize();
	void initRun();
	void toJson(json_t *rootJ);
	void fromJson(json_t *rootJ);
	void clockStep(bool realClockEdgeToHandle);
	inline void step() {
		clockPeriod++;
	}
	int keyIndexToGateTypeEx(int keyIndex);
	void transposeSeq(int seqn, int delta);
	void unTransposeSeq(int seqn) {
		transposeSeq(seqn, getTransposeOffset(seqn) * -1);
	}
	void rotateSeq(int seqn, int delta);	
	void unRotateSeq(int seqn) {
		rotateSeq(seqn, getRotateOffset(seqn) * -1);
	}

	
	private:
	
	
	void rotateSeqByOne(int seqn, bool directionRight);
	inline void propagateCVtoTied(int seqn, int stepn) {
		for (int i = stepn + 1; i < MAX_STEPS && attributes[seqn][i].getTied(); i++)
			cv[seqn][i] = cv[seqn][i - 1];	
	}
	void activateTiedStep(int seqn, int stepn);
	void deactivateTiedStep(int seqn, int stepn);
	void calcGateCodeEx(int seqn);
	bool moveStepIndexRun();
	void moveSongIndexBackward(bool init, bool rollover);
	void moveSongIndexForeward(bool init, bool rollover);
	int tempPhraseIndexes[MAX_PHRASES];// used only in next method	
	void moveSongIndexRandom(bool init, uint32_t randomValue);	
	void moveSongIndexBrownian(bool init, uint32_t randomValue);	
	void movePhraseIndexRun(bool init);
};// class SequencerKernel 


struct SeqCPbuffer {
	float cvCPbuffer[SequencerKernel::MAX_STEPS];// copy paste buffer for CVs
	StepAttributes attribCPbuffer[SequencerKernel::MAX_STEPS];
	SeqAttributes seqAttribCPbuffer;
	int storedLength;// number of steps that contain actual cp data
	
	SeqCPbuffer() {reset();}
	void reset();
};// struct SeqCPbuffer


struct SongCPbuffer {
	Phrase phraseCPbuffer[SequencerKernel::MAX_PHRASES];
	int beginIndex;
	int endIndex;
	int runModeSong;
	int storedLength;// number of steps that contain actual cp data
	
	SongCPbuffer() {reset();}
	void reset();
};// song SeqCPbuffer


//*****************************************************************************
// Sequencer
//*****************************************************************************


class Sequencer {
	public: 
	
	
	// General constants
	// ----------------

	// Sequencer dimensions
	static const int NUM_TRACKS = 4;
	static constexpr float gateTime = 0.4f;// seconds


	private:
	

	// Member data
	// ----------------	

	// Need to save
	int stepIndexEdit;
	int seqIndexEdit;// used in edit Seq mode only
	int phraseIndexEdit;// used in edit Song mode only
	int trackIndexEdit;
	SequencerKernel sek[NUM_TRACKS];
	
	// No need to save	
	unsigned long editingGate[NUM_TRACKS];// 0 when no edit gate, downward step counter timer when edit gate
	float editingGateCV[NUM_TRACKS];// no need to initialize, this goes with editingGate (output this only when editingGate > 0)
	int editingGateKeyLight;// no need to initialize, this goes with editingGate (use this only when editingGate > 0)
	SeqCPbuffer seqCPbuf;
	SongCPbuffer songCPbuf;
	int* velocityModePtr;
	
	
	public: 
	
	
	void construct(bool* _holdTiedNotesPtr, int* _velocityModePtr);
	

	inline int getStepIndexEdit() {return stepIndexEdit;}
	inline int getSeqIndexEdit() {return seqIndexEdit;}
	inline int getPhraseIndexEdit() {return phraseIndexEdit;}
	inline int getTrackIndexEdit() {return trackIndexEdit;}
	inline int getStepIndexRun(int trkn) {return sek[trkn].getStepIndexRun();}
	inline int getLength() {return sek[trackIndexEdit].getLength(seqIndexEdit);}
	inline StepAttributes getAttribute() {return sek[trackIndexEdit].getAttribute(seqIndexEdit, stepIndexEdit);}
	inline float getCV() {return sek[trackIndexEdit].getCV(seqIndexEdit, stepIndexEdit);}
	inline int keyIndexToGateTypeEx(int keyn) {return sek[trackIndexEdit].keyIndexToGateTypeEx(keyn);}
	inline int getGateType() {return sek[trackIndexEdit].getGateType(seqIndexEdit, stepIndexEdit);}
	inline int getPulsesPerStep() {return sek[trackIndexEdit].getPulsesPerStep();}
	inline int getDelay() {return sek[trackIndexEdit].getDelay();}
	inline int getRunModeSong() {return sek[trackIndexEdit].getRunModeSong();}
	inline int getRunModeSeq() {return sek[trackIndexEdit].getRunModeSeq(seqIndexEdit);}
	inline int getPhraseReps() {return sek[trackIndexEdit].getPhraseReps(phraseIndexEdit);}
	inline int getBegin() {return sek[trackIndexEdit].getBegin();}
	inline int getEnd() {return sek[trackIndexEdit].getEnd();}
	inline int getTransposeOffset() {return sek[trackIndexEdit].getTransposeOffset(seqIndexEdit);}
	inline int getRotateOffset() {return sek[trackIndexEdit].getRotateOffset(seqIndexEdit);}
	inline int getPhraseSeq() {return sek[trackIndexEdit].getPhraseSeq(phraseIndexEdit);}
	
	
	inline void setEditingGateKeyLight(int _editingGateKeyLight) {editingGateKeyLight = _editingGateKeyLight;}
	inline void setStepIndexEdit(int _stepIndexEdit, int sampleRate) {
		stepIndexEdit = _stepIndexEdit;
		if (!sek[trackIndexEdit].getTied(seqIndexEdit,stepIndexEdit)) {// play if non-tied step
			editingGate[trackIndexEdit] = (unsigned long) (gateTime * sampleRate / displayRefreshStepSkips);
			editingGateCV[trackIndexEdit] = sek[trackIndexEdit].getCV(seqIndexEdit, stepIndexEdit);
			editingGateKeyLight = -1;
		}
	}
	inline void setSeqIndexEdit(int _seqIndexEdit) {seqIndexEdit = _seqIndexEdit;}
	inline void setPhraseIndexEdit(int _phraseIndexEdit) {phraseIndexEdit = _phraseIndexEdit;}
	inline void setTrackIndexEdit(int _trackIndexEdit) {trackIndexEdit = _trackIndexEdit;}
	void setVelocityVal(int trkn, int intVel, int multiStepsCount, bool multiTracks);
	void setLength(int length, bool multiTracks);
	void setBegin(bool multiTracks);
	void setEnd(bool multiTracks);
	bool setGateType(int keyn, int multiSteps, bool autostepClick, bool multiTracks); // Third param is for right-click autostep. Returns success
	
	
	void initSlideVal(int multiStepsCount, bool multiTracks);
	void initGatePVal(int multiStepsCount, bool multiTracks);
	void initVelocityVal(int multiStepsCount, bool multiTracks);
	void initPulsesPerStep(bool multiTracks);
	void initDelay(bool multiTracks);
	void initRunModeSong(bool multiTracks);
	void initRunModeSeq(bool multiTracks);
	void initLength(bool multiTracks);
	void initPhraseReps(bool multiTracks);
	void initPhraseSeqNum(bool multiTracks);
	
	
	inline void incTrackIndexEdit() {
		if (trackIndexEdit < (NUM_TRACKS - 1)) trackIndexEdit++;
		else trackIndexEdit = 0;
	}
	inline void decTrackIndexEdit() {
		if (trackIndexEdit > 0) trackIndexEdit--;
		else trackIndexEdit = NUM_TRACKS - 1;
	}
	
	
	int getLengthSeqCPbug() {return seqCPbuf.storedLength;}
	void copySequence(int countCP);
	void pasteSequence(bool multiTracks);
	void copySong(int countCP);
	void pasteSong(bool multiTracks);
	
	
	void writeCV(int trkn, float cvVal, int multiStepsCount, float sampleRate, bool multiTracks);
	void autostep(bool autoseq);
	bool applyNewOctave(int octn, int multiSteps, float sampleRate, bool multiTracks); // returns true if tied
	bool applyNewKey(int keyn, int multiSteps, float sampleRate, bool autostepClick, bool multiTracks); // returns true if tied

	inline void moveStepIndexEdit(int delta) {
		stepIndexEdit = moveIndex(stepIndexEdit, stepIndexEdit + delta, SequencerKernel::MAX_STEPS);
	}
	void moveStepIndexEditWithEditingGate(int delta, bool writeTrig, float sampleRate);
	inline void moveSeqIndexEdit(int deltaSeqKnob) {
		seqIndexEdit = moveIndex(seqIndexEdit, seqIndexEdit + deltaSeqKnob, SequencerKernel::MAX_SEQS);
	}
	inline void movePhraseIndexEdit(int deltaPhrKnob) {
		phraseIndexEdit = moveIndex(phraseIndexEdit, phraseIndexEdit + deltaPhrKnob, SequencerKernel::MAX_PHRASES);
	}

	
	void modSlideVal(int deltaVelKnob, int mutliStepsCount, bool multiTracks);
	void modGatePVal(int deltaVelKnob, int mutliStepsCount, bool multiTracks);
	void modVelocityVal(int deltaVelKnob, int mutliStepsCount, bool multiTracks);
	void modRunModeSong(int deltaPhrKnob, bool multiTracks);
	void modPulsesPerStep(int deltaSeqKnob, bool multiTracks);
	void modDelay(int deltaSeqKnob, bool multiTracks);
	void modRunModeSeq(int deltaSeqKnob, bool multiTracks);
	void modLength(int deltaSeqKnob, bool multiTracks);
	void modPhraseReps(int deltaSeqKnob, bool multiTracks);
	void modPhraseSeqNum(int deltaSeqKnob, bool multiTracks);
	void transposeSeq(int deltaSeqKnob, bool multiTracks);
	void unTransposeSeq(bool multiTracks);
	void rotateSeq(int deltaSeqKnob, bool multiTracks);
	void unRotateSeq(bool multiTracks);

	void toggleGate(int multiSteps, bool multiTracks);
	bool toggleGateP(int multiSteps, bool multiTracks); // returns true if tied
	bool toggleSlide(int multiSteps, bool multiTracks); // returns true if tied
	void toggleTied(int multiSteps, bool multiTracks);


	inline float calcCvOutputAndDecSlideStepsRemain(int trkn, bool running) {
		float cvout;
		if (running)
			cvout = sek[trkn].getCVRun() - sek[trkn].calcSlideOffset();
		else
			cvout = (editingGate[trkn] > 0ul) ? editingGateCV[trkn] : sek[trkn].getCV(seqIndexEdit, stepIndexEdit);
		sek[trkn].decSlideStepsRemain();
		return cvout;
	}
	inline float calcGateOutput(int trkn, bool running, SchmittTrigger clockTrigger, float sampleRate) {
		if (running) 
			return (sek[trkn].calcGate(clockTrigger, sampleRate) ? 10.0f : 0.0f);
		return (editingGate[trkn] > 0ul) ? 10.0f : 0.0f;
	}
	inline float calcVelOutput(int trkn, bool running) {
		if (running)
			return calcVelocityVoltage(sek[trkn].getVelocityValRun());
		return calcVelocityVoltage(sek[trkn].getVelocityVal(seqIndexEdit, stepIndexEdit));
	}
	inline float calcVelocityVoltage(int vVal) {// internal use only, used by: calcVelOutput()
		float velRet = (float)vVal;
		if (*velocityModePtr == 0)
			velRet = velRet * 10.0f / 200.0f;
		else if (*velocityModePtr == 1)
			velRet = velRet * 10.0f / 127.0f;
		else
			velRet = velRet / 12.0f;
		return min(velRet, 10.0f);
	}
	inline float calcKeyLightWithEditing(int keyScanIndex, int keyLightIndex, float sampleRate) {
		if (editingGate[trackIndexEdit] > 0ul && editingGateKeyLight != -1)
			return (keyScanIndex == editingGateKeyLight ? ((float) editingGate[trackIndexEdit] / (float)(gateTime * sampleRate / displayRefreshStepSkips)) : 0.0f);
		return (keyScanIndex == keyLightIndex ? 1.0f : 0.0f);
	}
	
	
	inline void attach() {
		phraseIndexEdit = sek[trackIndexEdit].getPhraseIndexRun();
		seqIndexEdit = sek[trackIndexEdit].getPhraseSeq(phraseIndexEdit);
		stepIndexEdit = sek[trackIndexEdit].getStepIndexRun();
	}
	
	
	inline void stepEditingGate() {
		for (int trkn = 0; trkn < NUM_TRACKS; trkn++) {
			if (editingGate[trkn] > 0ul)
				editingGate[trkn]--;
		}
	}
	
	
	// Main methods
	// ----------------

	void reset();
	
	inline void randomize() {
		for (int trkn = 0; trkn < NUM_TRACKS; trkn++)
			sek[trkn].randomize();	
	}
	
	inline void initRun() {
		for (int trkn = 0; trkn < NUM_TRACKS; trkn++)
			sek[trkn].initRun();
	}
	
	void toJson(json_t *rootJ);
	void fromJson(json_t *rootJ);

	inline void clockStep(int trkn, bool realClockEdgeToHandle) {
		sek[trkn].clockStep(realClockEdgeToHandle);
	}
	inline void step() {
		for (int trkn = 0; trkn < NUM_TRACKS; trkn++) 
			sek[trkn].step();
	}
	
};// class Sequencer 

