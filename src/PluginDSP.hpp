#ifndef PLUGINDSP_HPP
#define PLUGINDSP_HPP

#include "DistrhoPlugin.hpp"
#include "Parameters.hpp"
#include "WinConsoleOutput.hpp"
#include "external/base64.h"
#include "Undo.hpp"
#include "external/Eigen/Dense"
#include <atomic>
#include <ctime>
#include "external/Eigen/SVD" // Make sure to include the SVD header at the top of your file
#include <random> // <-- ADD THIS LINE HERE

START_NAMESPACE_DISTRHO
    enum ActivationFunctionType{
    activationFunctionClip,
    activationFunctionTanh,
    activationFunctionNone,
    activationFunctionCount
};
static const char* activationFunctionNames[]={
        "Clip",
        "Tanh",
        "None"
    };

#define OUT_SIZE 30
#define STRIDE (OUT_SIZE)
#define MAX_DELAY 1000
class ImGuiPluginDSP : public Plugin
{
#define X(i) \
    float fInPan##i=0.0f;\
    float fInPOff##i=0.f;\
    float fFeed##i=0.f;\
    float fFreq##i=0.f;\
    float fOutPan##i=0.f;\
    float fOutPOff##i=0.f;\
    float fLvl##i=1.f/25.f;\
    float fPhs##i=0.f;

    BIQUAD_LIST
#undef X
    float fDelay=0.f;
    bool consoleAttached=false;
    std::mt19937 gen;
    std::normal_distribution<float> d;
    float note=1.f,lastNote=1.f;
public:

    std::atomic<ActivationFunctionType> activation=activationFunctionClip;
    float lastDelay=0;

    UndoItem *undoItems[MAX_UNDO_DEPTH];
    int nextUndoIndex=0;
    int undoCount=0,redoCount=0;

    Eigen::Array<std::complex<float>,25,1>inL, inR, outL, outR, lambda,state[MAX_DELAY+1];

    int inputBufferIndex=0;
    ImGuiPluginDSP()
        : Plugin(kParamCount, 0, 1) // parameters, programs, states
    {
        if (DEBUG&&!GetConsoleWindow()) {
            initConsoleOutput();
            consoleAttached=true;
        }

        for(int i=0;i<MAX_UNDO_DEPTH;i++)
        {
            undoItems[i]=NULL;
        }


        std::srand(std::time(nullptr));
        std::random_device rd;
        gen.seed(rd()); // Seed this specific instance with a hardware random
        std::cout<<"finished constructor"<<std::endl;
        calculateMatrix();
    }

    float findMaxAmplification(float* outPointer)
    {
        Eigen::Map<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE, Eigen::RowMajor>> W(outPointer);

        // Isolate the square audio feedback block (e.g., 24x24)
        Eigen::Matrix<float, OUT_SIZE, OUT_SIZE> audioBlock = W.block<OUT_SIZE, OUT_SIZE>(0, 0);

        // Compute the Singular Value Decomposition (SVD)
        // We only need the singular values, so we pass 0 to skip computing U and V matrices (saves CPU)
        Eigen::JacobiSVD<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE>> svd(audioBlock, 0);

        // The singular values are always returned sorted from highest to lowest.
        // Index 0 is mathematically guaranteed to be the maximum amplification factor!
        float maxAmplification = svd.singularValues()[0];

        std::cout << "Absolute Highest Single-Hop Amplification Factor: " << maxAmplification << std::endl;

        return maxAmplification;
    }


    void randomise()
    {
        std::complex<float> one=std::complex(1.f,0.f);
        inL(0)=one;inR(0)=one;
        lambda(0)=std::complex(0.5f,0.1f);
        outL(0)=one;outR(0)=one;
        Eigen::Array<float, 25, 1> matrix;
        matrix.setRandom();
        float noiseAmount=0.1f;
        inL+=matrix*noiseAmount;
        matrix.setRandom();

        outL+=matrix*noiseAmount;
        matrix.setRandom();

        inR+=matrix*noiseAmount;
        matrix.setRandom();

        outR+=matrix*noiseAmount;
        matrix.setRandom();

        lambda+=matrix*noiseAmount;

    }

    void printEigen(float *outPointer)
    {
        Eigen::Map<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE, Eigen::RowMajor>> W(outPointer);

        Eigen::EigenSolver<Eigen::Matrix<float, OUT_SIZE-2, OUT_SIZE-2>> solver(W.block<OUT_SIZE-2, OUT_SIZE-2>(2, 2), false);
        float maxMagnitude = 0.0f;
        for (int i = 0; i < OUT_SIZE-2; ++i) {
            maxMagnitude = std::max(maxMagnitude, std::abs(solver.eigenvalues()[i]));
        }
        std::cout<<"maxMAgnitude "<<maxMagnitude<<std::endl;
        //float mult=1/findMaxAmplification(outPointer);

    }




    ~ImGuiPluginDSP(){
        for(int i=0;i<MAX_UNDO_DEPTH;i++)
        {
            if(undoItems[i])delete undoItems[i];
        }
    }

protected:




    float clip(float a, float abs=1.f)
    {
        return std::max(-1.f*abs,std::min(1.f*abs,a));
    }
    inline float fastTanh(float x) {
        // Clamp input to prevent polynomial divergence at high values
        float x_clamped = std::max(-4.5f, std::min(4.5f, x));
        float x2 = x_clamped * x_clamped;

        // Pade approximation: highly accurate, no division loops, zero hardware stalls
        return x_clamped * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) /
               (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
    }
    void noteOn(int midiNote, float velocity){
        lastNote=note;
        note=std::pow(2.f, -(float)midiNote/12.f);
    }

    void noteOff(int midiNote){

    }

    void handleMidi(const MidiEvent *midiEvent){
        int status = midiEvent->data[0]; // midi status
        int midi_message = status & 0xF0;
        int midi_data1 = midiEvent->data[1];
        int midi_data2 = midiEvent->data[2];
        float velocity=(float)midi_data2;
        velocity/=128.f;
        switch ( midi_message )
        {
        case 0x80: // note_off
            noteOff(midi_data1);
            break;
        case 0x90: // note_on
            noteOn(midi_data1, velocity);
            break;
        }
    }
    void calculateMatrix()
    {
        std::cout<<"calculate"<<std::endl;

        float panAngle;
//pan: zero is 45 degrees, -M_PI/2 is zero degrees, M_PI/2 is 90 degrees
//left magnitude is cos(pan), right is sin(pan) (0)
//phase offset:0 to M_PI (0)
//freq: theta goes 0 t M_PI (0), freq from 0 to 24k
//feed: 0 to 1 (0)
//lvl: 0 to 1 (0)
//phase: 0 to M_PI

#define X(i) \
        \
        panAngle=M_PI/4.f+fInPan##i/2.f;\
        inL(i)=std::polar<float>(std::cos(panAngle),-fInPOff##i/2.f);\
        inR(i)=std::polar<float>(std::sin(panAngle),fInPOff##i/2.f);\
        lambda(i)=std::polar<float>(fFeed##i,fFreq##i*M_PI/24000.f);\
        panAngle=M_PI/4.f+fOutPan##i/2.f;\
        outL(i)=std::polar<float>(std::cos(panAngle),-fOutPOff##i/2);\
        outR(i)=std::polar<float>(std::sin(panAngle),fOutPOff##i/2);\
        outL(i)*=std::polar<float>(fLvl##i,fPhs##i);\
        outR(i)*=std::polar<float>(fLvl##i,fPhs##i);

        BIQUAD_LIST
#undef X
    }
    int count=0;
    void run ( const float **inputs, float **outputs, uint32_t frames,
             const MidiEvent *midiEvents, // MIDI pointer
             uint32_t midiEventCount      // Number of MIDI events in block
             ) override
    {
        // if(!(count++%1000))
        // {
        //     std::cout<<"run"<<std::endl;
        // }
        int curEventIndex =0;

        ActivationFunctionType activationFunction=activation.load(std::memory_order_release);


        float delay=std::max(0.f,std::min(fDelay*note,(float)MAX_DELAY));
        float currentLoopDelay=lastDelay;
        float lastLoopDelay=currentLoopDelay;


        for (uint32_t sample = 0; sample < frames; ++sample) {
            while ( curEventIndex < midiEventCount && sample == midiEvents[curEventIndex].frame )
            {
                handleMidi(&(midiEvents[curEventIndex++]));

            }
            if(note!=lastNote)
            {
                delay=std::max(0.f,std::min(fDelay*note,(float)MAX_DELAY));
                lastNote=note;
            }
            float temp=delay-lastDelay;
            float temp2=(temp*(float)(sample+1))/(float)frames;

            currentLoopDelay=lastDelay+temp2;

            int newIndex=(inputBufferIndex+1)%(MAX_DELAY+1);
            state[newIndex]=state[inputBufferIndex];
            inputBufferIndex=newIndex;
            state[inputBufferIndex]+=inL*inputs[0][sample]+inR*inputs[1][sample];
            state[inputBufferIndex]*=lambda;
            // state[inputBufferIndex].real() = state[inputBufferIndex].real().cwiseMax(-1.0f).cwiseMin(1.0f);
            // state[inputBufferIndex].imag() = state[inputBufferIndex].imag().cwiseMax(-1.0f).cwiseMin(1.0f);
            outputs[0][sample] = clip(2.f*std::real(outL.matrix().dot(state[inputBufferIndex].matrix())));
            outputs[1][sample] = clip(2.f*std::real(outR.matrix().dot(state[inputBufferIndex].matrix())));


            lastLoopDelay=currentLoopDelay;

        }
        lastDelay=lastLoopDelay;

    }

    // ----------------------------------------------------------------------------------------------------------------
    // Information
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

#define X(i) \
        if(index == kParamInPan##i) { \
                parameter.ranges = ParameterRanges(0.f, -M_PI/2, M_PI/2); \
                parameter.name = "Biquad " #i " InPan"; \
                parameter.symbol = "biquad_" #i "_in_pan"; \
                return; \
        } \
            if(index == kParamInPOff##i) { \
                parameter.ranges = ParameterRanges(0.f, 0.f, M_PI); \
                parameter.name = "Biquad " #i " InPOff"; \
                parameter.symbol = "biquad_" #i "_in_p_off"; \
                return; \
        } \
            if(index == kParamFeed##i) { \
                parameter.ranges = ParameterRanges(0.f, 0.f, 1.f); \
                parameter.name = "Biquad " #i " Feed"; \
                parameter.symbol = "biquad_" #i "_feed"; \
                return; \
        } \
            if(index == kParamFreq##i) { \
                parameter.ranges = ParameterRanges(0.f, 0.f, 24000.f); \
                parameter.name = "Biquad " #i " Freq"; \
                parameter.symbol = "biquad_" #i "_freq"; \
                return; \
        }\
        if(index == kParamOutPan##i) { \
        parameter.ranges = ParameterRanges(0.f, -M_PI/2, M_PI/2); \
                parameter.name = "Biquad " #i " OutPan"; \
                parameter.symbol = "biquad_" #i "_out_pan"; \
                return; \
        } \
            if(index == kParamOutPOff##i) { \
        parameter.ranges = ParameterRanges(0.f, 0.f, M_PI); \
                parameter.name = "Biquad " #i " OutPOff"; \
                parameter.symbol = "biquad_" #i "_out_p_off"; \
                return; \
        } \
            if(index == kParamLvl##i) { \
                parameter.ranges = ParameterRanges(1.f/25.f, 0.f, 1.f); \
                parameter.name = "Biquad " #i " Lvl"; \
                parameter.symbol = "biquad_" #i "_lvl"; \
                return; \
        } \
            if(index == kParamPhs##i) { \
                parameter.ranges = ParameterRanges(0.f, 0.f, M_PI); \
                parameter.name = "Biquad " #i " Phs"; \
                parameter.symbol = "biquad_" #i "_phs"; \
                return; \
        }
        BIQUAD_LIST
#undef X

            // Handle Delay
            if(index == kParamDelay) {
            parameter.ranges = ParameterRanges(0.f, 0.f, (float)MAX_DELAY);
            parameter.name = "Delay";
            parameter.symbol = "delay";
        }
    }

    float getParameterValue(uint32_t index) const override
    {

#define X(i) \
        if(index == kParamInPan##i) { \
                return fInPan##i; \
        } \
        if(index==kParamInPOff##i){\
            return fInPOff##i;\
        }\
        if(index==kParamFeed##i){\
            return fFeed##i;\
        }\
        if(index==kParamFreq##i){\
            return fFreq##i;\
        }\
        if(index == kParamOutPan##i) { \
                return fOutPan##i; \
        } \
            if(index==kParamOutPOff##i){\
                return fOutPOff##i;\
        }\
            if(index==kParamLvl##i){\
                return fLvl##i;\
        }\
            if(index==kParamPhs##i){\
                return fPhs##i;\
        }
        BIQUAD_LIST
#undef X

        if(index==kParamDelay){
            return fDelay;
        }
        std::cout<<"ERRORRRRRR   "<<index<<std::endl;
    }


    void setParameterValue(uint32_t index, float value) override
    {
        #define X(i) \
        if(index==kParamInPan##i){\
            fInPan##i=value;\
        }\
        if(index==kParamInPOff##i){\
            fInPOff##i=value;\
        }\
        if(index==kParamFeed##i){\
            fFeed##i=value;\
        }\
        if(index==kParamFreq##i){\
            fFreq##i=value;\
        }\
        if(index==kParamOutPan##i){\
                fOutPan##i=value;\
        }\
            if(index==kParamOutPOff##i){\
                fOutPOff##i=value;\
        }\
            if(index==kParamLvl##i){\
                fLvl##i=value;\
        }\
            if(index==kParamPhs##i){\
                fPhs##i=value;\
        }

        BIQUAD_LIST
#undef X


        if(index==kParamDelay){
            fDelay=value;
        }
        std::cout<<"all set"<<std::endl;
        calculateMatrix();
    }

    void activate() override
    {
    }

    void initState(uint32_t index, String& key, String& defaultValue) override
    {
        if (index == 0) {
            key = "sampleData";
            defaultValue = "";
        }
    }

    String getState(const char* key) const override {
        // if (!strcmp(key,"sampleData"))
        // {

        //     size_t maxEigenSize=sizeof(float);
        //     size_t activationSize=sizeof(ActivationFunctionType);
        //     size_t matrixSize=sizeof(float)*(OUT_SIZE)*OUT_SIZE;

        //     size_t totalBytes = maxEigenSize+activationSize+matrixSize;

        //     std::vector<uint8_t> rawBinaryBuffer(totalBytes);
        //     auto *incrementalPointer=rawBinaryBuffer.data();

        //     auto *maxEigenPointer=reinterpret_cast<float*>(incrementalPointer);
        //     *maxEigenPointer=max_eigenvalue;
        //     incrementalPointer+=maxEigenSize;

        //     auto* activationPointer=reinterpret_cast<ActivationFunctionType*>(incrementalPointer);
        //     *activationPointer=activation.load(std::memory_order_relaxed);
        //     incrementalPointer+=activationSize;

        //     auto *matrixPtr=reinterpret_cast<float*>(incrementalPointer);
        //     auto *buffer=currentBufferPointer.load(std::memory_order_relaxed);
        //     for(int i=0;i<OUT_SIZE*(OUT_SIZE);i++)
        //     {
        //         matrixPtr[i]=buffer[i];
        //     }

        //     std::string encodedText = base64_encode(rawBinaryBuffer.data(), rawBinaryBuffer.size());
        //     return String(encodedText.c_str());
        // }else
            return String("");

    }

    void setState(const char *key, const char * value){
        // if(!strcmp("sampleData", key))
        // {
        //     if (strlen(value) == 0 ) {
        //         return;
        //     }


        //     std::string decodedBytes = base64_decode(std::string(value));

        //     const uint8_t* incrementalPointer = reinterpret_cast<const uint8_t*>(decodedBytes.data());

        //     size_t maxEigenSize=sizeof(float);
        //     size_t activationSize=sizeof(ActivationFunctionType);
        //     size_t matrixSize=sizeof(float)*(OUT_SIZE)*OUT_SIZE;


        //     auto *maxEigenPointer=reinterpret_cast<const float*>(incrementalPointer);
        //     max_eigenvalue=*maxEigenPointer;
        //     incrementalPointer+=maxEigenSize;

        //     auto* activationPointer=reinterpret_cast<const ActivationFunctionType*>(incrementalPointer);
        //     activation.store(*activationPointer,std::memory_order_relaxed);
        //     incrementalPointer+=activationSize;

        //     auto *matrixPtr=reinterpret_cast<const float*>(incrementalPointer);
        //     updateReady.store(false,std::memory_order_release);
        //     auto *buffer=currentBufferPointer.load(std::memory_order_acquire);
        //     float *outPointer=(buffer==weightBuffer1?weightBuffer2:weightBuffer1);
        //     for(int i=0;i<OUT_SIZE*(OUT_SIZE);i++)
        //     {
        //         outPointer[i]=matrixPtr[i];
        //     }
        //     currentBufferPointer.store(outPointer,std::memory_order_release);

        // }
    }
    /**
      Get the plugin label.@n
      This label is a short restricted name consisting of only _, a-z, A-Z and 0-9 characters.
    */
    const char* getLabel() const noexcept override
    {
        return "Biquad Bank";
    }

    /**
      Get an extensive comment/description about the plugin.@n
      Optional, returns nothing by default.
    */
    const char* getDescription() const override
    {
        return "Biquad bank";
    }

    /**
      Get the plugin author/maker.
    */
    const char* getMaker() const noexcept override
    {
        return "Jean Pierre Cimalando, falkTX, Saber";
    }

    /**
      Get the plugin license (a single line of text or a URL).@n
      For commercial plugins this should return some short copyright information.
    */
    const char* getLicense() const noexcept override
    {
        return "ISC";
    }

    /**
      Get the plugin version, in hexadecimal.
      @see d_version()
    */
    uint32_t getVersion() const noexcept override
    {
        return d_version(1, 0, 0);
    }

    /**
      Get the plugin unique Id.@n
      This value is used by LADSPA, DSSI and VST plugin formats.
      @see d_cconst()
    */
    int64_t getUniqueId() const noexcept override
    {
        return d_cconst('B', 'q', 'B', 'k');
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Init
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImGuiPluginDSP)
};

END_NAMESPACE_DISTRHO

#endif // PLUGINUI_HPP
