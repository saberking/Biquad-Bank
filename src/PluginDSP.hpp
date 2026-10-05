#ifndef PLUGINDSP_HPP
#define PLUGINDSP_HPP

#include "DistrhoPlugin.hpp"
#include "Parameters.hpp"
#include "WinConsoleOutput.hpp"
#include "external/base64.h"
#include "Undo.hpp"
#include "external/Eigen/Dense"
#include <atomic>

START_NAMESPACE_DISTRHO
#define OUT_SIZE 36
#define CONSTANT_KNOB_COUNT 4
#define STRIDE (OUT_SIZE+CONSTANT_KNOB_COUNT)
#define MAX_EIGENVALUE 1.f
class ImGuiPluginDSP : public Plugin
{
    float fA = 0.0f;
    float fB=0.f;
    float fC=0.f;
    float fD=0.f;
    bool consoleAttached=false;
public:



    UndoItem *undoItems[MAX_UNDO_DEPTH];
    int nextUndoIndex=0;
    int undoCount=0,redoCount=0;
    alignas(64) float weightBuffer1[OUT_SIZE*(OUT_SIZE+CONSTANT_KNOB_COUNT)];
    alignas(64) float weightBuffer2[OUT_SIZE*(OUT_SIZE+CONSTANT_KNOB_COUNT)];
    std::atomic<float *> weightBufferPointer;
    alignas(64) float inputBuffer[OUT_SIZE+CONSTANT_KNOB_COUNT];
    alignas(64) float outputBuffer[OUT_SIZE];
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

        for(int i=0;i<OUT_SIZE*(OUT_SIZE+CONSTANT_KNOB_COUNT);i++)
        {
            weightBuffer1[i]=0.f;
        }

        for(int i=0;i<2;i++)
        {
            weightBuffer1[i*(OUT_SIZE+CONSTANT_KNOB_COUNT+1)]=1.f;
        }
        weightBufferPointer.store(weightBuffer1,std::memory_order_relaxed);
    }
    void randomise()
    {
        float *inPointer, *outPointer;
        inPointer=weightBufferPointer.load(std::memory_order_relaxed);
        outPointer=(inPointer==weightBuffer1?weightBuffer2:weightBuffer1);

        for(int i=0;i<OUT_SIZE*(OUT_SIZE+CONSTANT_KNOB_COUNT);i++)
        {
            outPointer[i]=inPointer[i];//+(i%3?0.1:-0.1);
        }


        // outPointer[OUT_SIZE-2]=1.f;
        // outPointer[(OUT_SIZE+CONSTANT_KNOB_COUNT)+OUT_SIZE-1]=1.f;
        // for(int i =2;i<OUT_SIZE;i++)
        // {
        //     outPointer[(OUT_SIZE+CONSTANT_KNOB_COUNT)*i+i-2]=1.f;

        // }
        Eigen::Map<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE+CONSTANT_KNOB_COUNT, Eigen::RowMajor>> W(outPointer);
        Eigen::Matrix<float, OUT_SIZE, OUT_SIZE> squareW = W.block<OUT_SIZE, OUT_SIZE>(0, 0);
        // 3. Generate a random matrix using Eigen's built-in fast generator
        Eigen::Matrix<float, OUT_SIZE, OUT_SIZE> X = Eigen::Matrix<float, OUT_SIZE, OUT_SIZE>::Random();

        // 4. Householder QR Decomposition (The Math Trick)
        // QR decomposition breaks any random matrix X into:
        // Q (a perfect, pure orthogonal rotation matrix) and R (upper triangular).
        Eigen::HouseholderQR<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE>> qr(X);
        Eigen::Matrix<float, OUT_SIZE, OUT_SIZE> Q = qr.householderQ();
        W.block<OUT_SIZE, OUT_SIZE>(0, 0) = W.block<OUT_SIZE, OUT_SIZE>(0, 0) * Q;


 // W.diagonal().setConstant(0.1f);//witout this line it ccrash!!!!!!!!!!!!!
        const float noiseAmount = 1.f;

        // 1. Add noise safely to the main audio feedback block
        W.block<OUT_SIZE, OUT_SIZE>(0, 0) += Eigen::Matrix<float, OUT_SIZE, OUT_SIZE>::Random() * noiseAmount;

        // 2. Add noise safely to the parameter tracking columns
        // This completely isolates the knob section so the compiler optimizer cannot glitch
        W.block<OUT_SIZE, CONSTANT_KNOB_COUNT>(0, OUT_SIZE) += Eigen::Matrix<float, OUT_SIZE, CONSTANT_KNOB_COUNT>::Random() * noiseAmount;


        Eigen::EigenSolver<Eigen::Matrix<float, OUT_SIZE-2, OUT_SIZE-2>> solver(W.block<OUT_SIZE-2, OUT_SIZE-2>(2, 2), false);
        float maxMagnitude = 0.0f;
        for (int i = 0; i < OUT_SIZE-2; ++i) {
            maxMagnitude = std::max(maxMagnitude, std::abs(solver.eigenvalues()[i]));
        }
        maxMagnitude/=MAX_EIGENVALUE;
        maxMagnitude+=0.01f;
        if (maxMagnitude >= 1.0f) {
            W /= maxMagnitude;
        }

        // float det = squareW.determinant();
        // if (det > 0.0f) // The determinant must be positive to take an even root safely
        // {
        //     float root = std::pow(det, 1.0f / (float) OUT_SIZE);
        //     W /= root;
        // }else if(det<0.0f)
        // {
        //     float root = -std::pow(std::abs(det), 1.0f / (float) OUT_SIZE);
        //     W /= root;
        // }
        weightBufferPointer.store(outPointer,std::memory_order_relaxed);
    }
    ~ImGuiPluginDSP(){
        for(int i=0;i<MAX_UNDO_DEPTH;i++)
        {
            if(undoItems[i])delete undoItems[i];
        }
    }

protected:


    /**
      Initialize the parameter @a index.@n
      This function will be called once, shortly after the plugin is created.
    */
    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if(index==kParamA)
        {
            parameter.ranges.min = -1.f;
            parameter.ranges.max = 1.f;
            parameter.ranges.def = 0.f;
            parameter.name = "A";
            parameter.symbol = "A";
            parameter.hints=kParameterIsAutomatable;
        }
        if(index==kParamB)
        {
            parameter.ranges.min = -1.f;
            parameter.ranges.max = 1.f;
            parameter.ranges.def = 0.f;
            parameter.name = "B";
            parameter.symbol = "B";
            parameter.hints=kParameterIsAutomatable;
        }
        if(index==kParamC)
        {
            parameter.ranges.min = -1.f;
            parameter.ranges.max = 1.f;
            parameter.ranges.def = 0.f;
            parameter.name = "C";
            parameter.symbol = "C";
            parameter.hints=kParameterIsAutomatable;
        }
        if(index==kParamD)
        {
            parameter.ranges.min = -1.f;
            parameter.ranges.max = 1.f;
            parameter.ranges.def = 0.f;
            parameter.name = "D";
            parameter.symbol = "D";
            parameter.hints=kParameterIsAutomatable;
        }


    }

    float getParameterValue(uint32_t index) const override
    {
        if(index==kParamA){
            return fA;
        }
        if(index==kParamB){
            return fB;
        }
        if(index==kParamC){
            return fC;
        }
        if(index==kParamD){
            return fD;
        }
    }


    void setParameterValue(uint32_t index, float value) override
    {
        if(index==kParamA){
            fA=value;
        }
        if(index==kParamB){
            fB=value;
        }
        if(index==kParamC){
            fC=value;
        }
        if(index==kParamD){
            fD=value;
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


            size_t totalBytes = 0;

            std::vector<uint8_t> rawBinaryBuffer(totalBytes);
            auto *incrementalPointer=rawBinaryBuffer.data();


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

            // 2. Read the sample length header out of the first 4 bytes

        }
    }




    void run ( const float **inputs, float **outputs, uint32_t frames) override
    {
            // Eigen::setNbThreads(1);
        // Explicitly pass 'Eigen::Unaligned' as the template argument to protect AVX fetches
        Eigen::Map<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE+CONSTANT_KNOB_COUNT, Eigen::RowMajor>, Eigen::Aligned64>
            W(weightBufferPointer.load(std::memory_order_relaxed));

        // Eigen::Map<Eigen::Matrix<float, OUT_SIZE, OUT_SIZE+CONSTANT_KNOB_COUNT,
        //                          Eigen::RowMajor>> W(weightBufferPointer.load(std::memory_order_relaxed));
        Eigen::Map<Eigen::Vector<float, OUT_SIZE+CONSTANT_KNOB_COUNT>> x(inputBuffer);
        Eigen::Map<Eigen::Vector<float, OUT_SIZE>> y(outputBuffer);

        for (uint32_t sample = 0; sample < frames; ++sample) {
            // 1. Load your sample into your input vector 'x' here...
            x[0]=inputs[0][sample];x[1]=inputs[1][sample];

            // 2. Execute the SIMD-accelerated math
            // y.noalias() = W * x;crash

            // y.noalias() = W.lazyProduct(x);even slower

            // for (int i = 0; i < OUT_SIZE; ++i) {//doesnbt crash
            //     y[i] = W.row(i).dot(x);
            // }

            const float* rawW = W.data();
            const float* rawX = x.data();
            float*       rawY = y.data();


            // 2. Your ultra-fast, thread-safe unrolled loop compiles perfectly now!
            for (int r = 0; r < OUT_SIZE; ++r) {
                float sum = 0.0f;

                // Use rawW instead of the Eigen object W
                const float* rowPtr = &rawW[r * STRIDE];

                for (int c = 0; c < STRIDE; ++c) {
                    sum += rowPtr[c] * rawX[c];
                }
                if(r>2&&sum>0.5){
                    sum=0.25+sum/2;
                }
                if(r>2&&sum<-0.5)
                {
                    sum=-0.25+sum/2;
                }

                rawY[r] = sum;
            }
            // y = y.array().cwiseMax(-1.0f).cwiseMin(1.0f);

            // y = y.unaryExpr([](float val) {
            //     if (val > 1.25f)  return 1.0f;
            //     if (val < -1.25f) return -1.0f;
            //     // Cubic polynomial: smooth S-curve transition
            //     return val * (1.0f - 0.16f * val * val);
            // });

            outputs[0][sample]=y[0];outputs[1][sample]=y[1];
            // FIX: Adding .eval() forces the compiler to completely finish your loops
            // and evaluate 'y' into a safe register state before writing a single bit into 'x'.
            x.head<OUT_SIZE>() = y.eval();
            // x.head<OUT_SIZE>() = y;

            // Append your 4 special parameters to the remaining 4 slots of x
            x[OUT_SIZE] = fA;
            x[OUT_SIZE+1] = fB;
            x[OUT_SIZE+2] = fC;
            x[OUT_SIZE+3] = fD;
        }

    }

    // ----------------------------------------------------------------------------------------------------------------
    // Information

    /**
      Get the plugin label.@n
      This label is a short restricted name consisting of only _, a-z, A-Z and 0-9 characters.
    */
    const char* getLabel() const noexcept override
    {
        return "neural";
    }

    /**
      Get an extensive comment/description about the plugin.@n
      Optional, returns nothing by default.
    */
    const char* getDescription() const override
    {
        return "neural FX";
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
        return d_cconst('n', 'e', 'u', 'r');
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Init
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImGuiPluginDSP)
};

END_NAMESPACE_DISTRHO

#endif // PLUGINUI_HPP
