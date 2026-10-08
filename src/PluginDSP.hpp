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
    float fLvl##i=0.f;\
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

    Eigen::Array<float, 25, MAX_DELAY+1>XVector, YVector;
    Eigen::Array<float, 25, 1> rCosTheta, rSinTheta;
    Eigen::Array<float, 25,1>outputVectorX, outputVectorY;
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

        XVector.row(0).setZero();
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


    void normaliseMatrix(float *outPointer)
    {
        Eigen::Map<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE, Eigen::RowMajor>> W(outPointer);

        Eigen::EigenSolver<Eigen::Matrix<float, OUT_SIZE-2, OUT_SIZE-2>> solver(W.block<OUT_SIZE-2, OUT_SIZE-2>(2, 2), false);
        float maxMagnitude = 0.0f;
        for (int i = 0; i < OUT_SIZE-2; ++i) {
            maxMagnitude = std::max(maxMagnitude, std::abs(solver.eigenvalues()[i]));
        }
        std::cout<<"normalise"<<maxMagnitude<<std::endl;
        //maxMagnitude+=0.001f;
        float mult=max_eigenvalue/std::max(0.001f,maxMagnitude);

        //float mult=1/findMaxAmplification(outPointer);

        W *=mult;

        // for (int r = 0; r < OUT_SIZE; ++r) {
        //     for (int c = 0; c < OUT_SIZE; ++c) {
        //         if (std::abs(W(r, c)) > MAX_EIGENVALUE) {
        //             // Keep the original positive/negative sign but snap the value to 0.95f
        //             W(r, c) = std::copysign(MAX_EIGENVALUE, W(r, c));
        //         }
        //     }
        // }
        // W.block<OUT_SIZE,CONSTANT_KNOB_COUNT>(0,OUT_SIZE).setZero();
        W.block<2,OUT_SIZE>(0,0).rowwise().normalize();
        //         W.block<OUT_SIZE,2>(0,0)*=0.95f;
        // W.block<2,OUT_SIZE>(0,0)*=0.95f;


        printEigen(outPointer);
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

    bool normalise()
    {
        if(updateReady.load(std::memory_order_acquire))return true;
        float *inPointer, *outPointer;
        inPointer=currentBufferPointer.load(std::memory_order_acquire);
        outPointer=(inPointer==weightBuffer1?weightBuffer2:weightBuffer1);

        for(int i=0;i<OUT_SIZE*(OUT_SIZE);i++)
        {
            outPointer[i]=inPointer[i];//+(i%3?0.1:-0.1);
        }
        normaliseMatrix(outPointer);

        updateReady.store(true,std::memory_order_release);
        return false;

    }
    void randomise()
    {
        if(updateReady.load(std::memory_order_acquire))return;
        float *inPointer, *outPointer;
        inPointer=currentBufferPointer.load(std::memory_order_acquire);
        outPointer=(inPointer==weightBuffer1?weightBuffer2:weightBuffer1);

        for(int i=0;i<OUT_SIZE*(OUT_SIZE);i++)
        {
            outPointer[i]=inPointer[i];//+(i%3?0.1:-0.1);
        }



        Eigen::Map<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE, Eigen::RowMajor>> W(outPointer);

        Eigen::MatrixXf X(OUT_SIZE, OUT_SIZE);
        for (int r = 0; r < OUT_SIZE; ++r) {
            for (int c = 0; c < OUT_SIZE; ++c) {
                X(r, c) = d(gen); // Populating matrix with a true Gaussian profile
            }
        }

        // A Householder QR acting on a Gaussian matrix produces a perfectly un-biased
        // Haar-distributed random orthogonal matrix, eliminating Left/Right panning bias!
        Eigen::HouseholderQR<Eigen::MatrixXf> qr(X);
        Eigen::MatrixXf Q = qr.householderQ();

        W.block<OUT_SIZE, OUT_SIZE>(0, 0) = (W.block<OUT_SIZE, OUT_SIZE>(0, 0) * Q * 0.1f).eval()
                                            + (W.block<OUT_SIZE, OUT_SIZE>(0, 0) * 0.9f).eval();

        // Fix noise injection using the same Gaussian distribution profile
        Eigen::MatrixXf noise(OUT_SIZE, OUT_SIZE);
        const float noiseAmount = 0.05f;
        for (int r = 0; r < OUT_SIZE; ++r) {
            for (int c = 0; c < OUT_SIZE; ++c) {
                noise(r, c) = d(gen) * noiseAmount;
            }
        }
    W.block<OUT_SIZE, OUT_SIZE>(0, 0) += noise;
        normaliseMatrix(outPointer);

        updateReady.store(true,std::memory_order_release);

    }
    void delay()
    {
        if(updateReady.load(std::memory_order_acquire)) return;
        float *inPointer, *outPointer;
        inPointer=currentBufferPointer.load(std::memory_order_acquire);
        outPointer=(inPointer==weightBuffer1?weightBuffer2:weightBuffer1);

        for(int i=0;i<OUT_SIZE*(OUT_SIZE);i++)
        {
            outPointer[i]=inPointer[i];//+(i%3?0.1:-0.1);
        }


        // outPointer[OUT_SIZE-2]=1.f;
        // outPointer[(OUT_SIZE)+OUT_SIZE-1]=1.f;
        // for(int i =2;i<OUT_SIZE;i++)
        // {
        //     outPointer[(OUT_SIZE)*i+i-2]=1.f;

        // }
        Eigen::Map<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE, Eigen::RowMajor>> W(outPointer);


        auto circularSequence = (Eigen::VectorXi::LinSpaced(OUT_SIZE, -2, OUT_SIZE - 3).array() + OUT_SIZE)
                                    .unaryExpr([](int val) { return val % OUT_SIZE; });

        // 2. COMPILER-SAFE REWRITE: Create a temporary copy of the old matrix state
        // This completely stops memory aliasing bugs without needing Eigen::all!
        Eigen::Matrix<float, OUT_SIZE, OUT_SIZE , Eigen::RowMajor> tempW = W;

        // 3. Remap the rows sequentially
        for (int i = 0; i < OUT_SIZE; ++i) {
            int targetRowIndex = circularSequence[i];
            W.row(i) += tempW.row(targetRowIndex)*0.2f; // Cleanly assigns the whole row
        }

        normaliseMatrix(outPointer);

        updateReady.store(true,std::memory_order_release);



    }



    void printMatrix()
    {
        for(int i=0;i<OUT_SIZE;i++){
            for(int j=0;j<OUT_SIZE;j++)        std::cout<<currentBufferPointer.load(std::memory_order_acquire)[i*(OUT_SIZE)+j]<<" ";
            std::cout<<std::endl;
        }
        printEigen(currentBufferPointer.load(std::memory_order_acquire));
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
#define X(i) \
        outputVectorX(i)=std::cos(fD##i)*fC##i;\
        outputVectorY(i)=std::sin(fD##i)*fC##i;\
        \
        rCosTheta(i)=std::cos(fB##i)*fA##i;\
        rSinTheta(i)=std::sin(fB##i)*fA##i;

        BIQUAD_LIST
#undef X
    }

    void run ( const float **inputs, float **outputs, uint32_t frames,
             const MidiEvent *midiEvents, // MIDI pointer
             uint32_t midiEventCount      // Number of MIDI events in block
             ) override
    {
        int curEventIndex =0;

        ActivationFunctionType activationFunction=activation.load(std::memory_order_release);

        // if(updateReady.load(std::memory_order_acquire))
        // {
        //     float *newPointer=(currentBufferPointer.load(std::memory_order_acquire)==weightBuffer1?weightBuffer2:weightBuffer1);
        //     currentBufferPointer.store(newPointer,std::memory_order_release);
        //     updateReady.store(false,std::memory_order_release);

        // }
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
            int newIndex=(inputBufferIndex+lastLoopDelay-currentLoopDelay+MAX_DELAY+1)%(MAX_DELAY+1);
            if(newIndex<0)std::cout<<"ERRORR!!!!!"<<"ERRRORRRR!!!!!!"<<std::endl<<"current "<<
                          currentLoopDelay<<" lastD "<<lastDelay<<"   delay "<<delay<<"    sammple "<<sample<<"    frames"<<frames<<
                    "    temp"<<temp<<"    temp2 "<<temp2<<  std::endl;
            else inputBufferIndex=newIndex;
            lastLoopDelay=currentLoopDelay;



            XVector.column(inputBufferIndex)+=inputs[0];






            if(activationFunction==activationFunctionClip)
            {
                y = y.array().cwiseMax(-1.0f).cwiseMin(1.0f);

            }
            if(activationFunction==activationFunctionTanh)
            {
                float* rawY = y.data();
                for (int r = 0; r < OUT_SIZE; ++r) {
                    rawY[r] = fastTanh(rawY[r]);
                }
            }



            outputs[0][sample]=clip(y[0],2);outputs[1][sample]=clip(y[1],2);

            inputBufferIndex=(inputBufferIndex+1)%(MAX_DELAY+1);
            Eigen::Map<Eigen::Vector<float, OUT_SIZE>> x2(inputBuffer[(inputBufferIndex+currentLoopDelay)%(MAX_DELAY+1)]);
            x2.head<OUT_SIZE>() = y.eval();


        }
        lastDelay=lastLoopDelay;

    }

    // ----------------------------------------------------------------------------------------------------------------
    // Information
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

// Handle standard biquad parameter generations
#define X(i) \
        if(index == kParamInPan##i) { \
                parameter.ranges = ParameterRanges(0.f, -1.f, 1.f); \
                parameter.name = "Biquad " #i " InPan"; \
                parameter.symbol = "biquad_" #i "_in_pan"; \
                return; \
        } \
            if(index == kParamInPOff##i) { \
                parameter.ranges = ParameterRanges(0.f, -1.f, 1.f); \
                parameter.name = "Biquad " #i " InPOff"; \
                parameter.symbol = "biquad_" #i "_in_p_off"; \
                return; \
        } \
            if(index == kParamFeed##i) { \
                parameter.ranges = ParameterRanges(0.f, -1.f, 1.f); \
                parameter.name = "Biquad " #i " Feed"; \
                parameter.symbol = "biquad_" #i "_feed"; \
                return; \
        } \
            if(index == kParamFreq##i) { \
                parameter.ranges = ParameterRanges(0.f, -1.f, 1.f); \
                parameter.name = "Biquad " #i " Freq"; \
                parameter.symbol = "biquad_" #i "_freq"; \
                return; \
        }\
        if(index == kParamOutPan##i) { \
                parameter.ranges = ParameterRanges(0.f, -1.f, 1.f); \
                parameter.name = "Biquad " #i " OutPan"; \
                parameter.symbol = "biquad_" #i "_out_pan"; \
                return; \
        } \
            if(index == kParamOutPOff##i) { \
                parameter.ranges = ParameterRanges(0.f, -1.f, 1.f); \
                parameter.name = "Biquad " #i " OutPOff"; \
                parameter.symbol = "biquad_" #i "_out_p_off"; \
                return; \
        } \
            if(index == kParamLvl##i) { \
                parameter.ranges = ParameterRanges(0.f, -1.f, 1.f); \
                parameter.name = "Biquad " #i " Lvl"; \
                parameter.symbol = "biquad_" #i "_lvl"; \
                return; \
        } \
            if(index == kParamPhs##i) { \
                parameter.ranges = ParameterRanges(0.f, -1.f, 1.f); \
                parameter.name = "Biquad " #i " Phs"; \
                parameter.symbol = "biquad_" #i "_phs"; \
                return; \
        }
        BIQUAD_LIST
#undef X

            // Handle Delay
            if(index == kParamFreqelay) {
            parameter.ranges = ParameterRanges(0.f, 0.f, MAX_DELAY);
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
            if(index==kParamFeed##i){\
                return fFeed##i;\
        }\
            if(index==kParamFreq##i){\
                return fFreq##i;\
        }
        BIQUAD_LIST
#undef X

        if(index==kParamFreqelay){
            return fDelay;
        }
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


        if(index==kParamFreqelay){
            fDelay=value;
        }


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
        if (!strcmp(key,"sampleData"))
        {

            size_t maxEigenSize=sizeof(float);
            size_t activationSize=sizeof(ActivationFunctionType);
            size_t matrixSize=sizeof(float)*(OUT_SIZE)*OUT_SIZE;

            size_t totalBytes = maxEigenSize+activationSize+matrixSize;

            std::vector<uint8_t> rawBinaryBuffer(totalBytes);
            auto *incrementalPointer=rawBinaryBuffer.data();

            auto *maxEigenPointer=reinterpret_cast<float*>(incrementalPointer);
            *maxEigenPointer=max_eigenvalue;
            incrementalPointer+=maxEigenSize;

            auto* activationPointer=reinterpret_cast<ActivationFunctionType*>(incrementalPointer);
            *activationPointer=activation.load(std::memory_order_relaxed);
            incrementalPointer+=activationSize;

            auto *matrixPtr=reinterpret_cast<float*>(incrementalPointer);
            auto *buffer=currentBufferPointer.load(std::memory_order_relaxed);
            for(int i=0;i<OUT_SIZE*(OUT_SIZE);i++)
            {
                matrixPtr[i]=buffer[i];
            }

            std::string encodedText = base64_encode(rawBinaryBuffer.data(), rawBinaryBuffer.size());
            return String(encodedText.c_str());
        }else return String("");

    }

    void setState(const char *key, const char * value){
        if(!strcmp("sampleData", key))
        {
            if (strlen(value) == 0 ) {
                return;
            }


            std::string decodedBytes = base64_decode(std::string(value));

            const uint8_t* incrementalPointer = reinterpret_cast<const uint8_t*>(decodedBytes.data());

            size_t maxEigenSize=sizeof(float);
            size_t activationSize=sizeof(ActivationFunctionType);
            size_t matrixSize=sizeof(float)*(OUT_SIZE)*OUT_SIZE;


            auto *maxEigenPointer=reinterpret_cast<const float*>(incrementalPointer);
            max_eigenvalue=*maxEigenPointer;
            incrementalPointer+=maxEigenSize;

            auto* activationPointer=reinterpret_cast<const ActivationFunctionType*>(incrementalPointer);
            activation.store(*activationPointer,std::memory_order_relaxed);
            incrementalPointer+=activationSize;

            auto *matrixPtr=reinterpret_cast<const float*>(incrementalPointer);
            updateReady.store(false,std::memory_order_release);
            auto *buffer=currentBufferPointer.load(std::memory_order_acquire);
            float *outPointer=(buffer==weightBuffer1?weightBuffer2:weightBuffer1);
            for(int i=0;i<OUT_SIZE*(OUT_SIZE);i++)
            {
                outPointer[i]=matrixPtr[i];
            }
            currentBufferPointer.store(outPointer,std::memory_order_release);

        }
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
