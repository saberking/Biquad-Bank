#ifndef PLUGINDSP_HPP
#define PLUGINDSP_HPP

#include "DistrhoPlugin.hpp"
#include "Parameters.hpp"
#include "WinConsoleOutput.hpp"
#include "external/base64.h"
#include "Undo.hpp"

START_NAMESPACE_DISTRHO

class ImGuiPluginDSP : public Plugin
{
    float fA = 1.0f;
    float fB=1.f;
    float fC=1.f;
    float fD=0.25f;
    bool consoleAttached=false;
public:


    UndoItem *undoItems[MAX_UNDO_DEPTH];
    int nextUndoIndex=0;
    int undoCount=0,redoCount=0;

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
            parameter.ranges.def = 1.f;
            parameter.name = "A";
            parameter.symbol = "A";
            parameter.hints=kParameterIsAutomatable;
        }
        if(index==kParamB)
        {
            parameter.ranges.min = -1.f;
            parameter.ranges.max = 1.f;
            parameter.ranges.def = 1.f;
            parameter.name = "B";
            parameter.symbol = "B";
            parameter.hints=kParameterIsAutomatable;
        }
        if(index==kParamC)
        {
            parameter.ranges.min = -1.f;
            parameter.ranges.max = 1.f;
            parameter.ranges.def = 1.f;
            parameter.name = "C";
            parameter.symbol = "C";
            parameter.hints=kParameterIsAutomatable;
        }
        if(index==kParamD)
        {
            parameter.ranges.min = -1.f;
            parameter.ranges.max = 1.f;
            parameter.ranges.def = 0.25f;
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

        for ( uint32_t i = 0; i < frames; i++ )
        {


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
