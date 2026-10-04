/*
 * ImGui plugin example
 * Copyright (C) 2021 Jean Pierre Cimalando <jp-dev@inbox.ru>
 * Copyright (C) 2021-2022 Filipe Coelho <falktx@falktx.com>
 * SPDX-License-Identifier: ISC
 */

#include "DistrhoUI.hpp"
#include "ResizeHandle.hpp"
#include "Parameters.hpp"

#include <../clap/include/clap/ext/context-menu.h>
#include <../clap/include/clap/ext/state.h>
#include <../clap/include/clap/ext/params.h>
#include <windows.h>
#include <commctrl.h> // For SetWindowSubclass API
#include "PluginDSP.hpp"
#pragma comment(lib, "comctl32.lib")


START_NAMESPACE_DISTRHO


//for right click autmoaiton clip
class ImGuiPluginUI : public UI
{
    ResizeHandle fResizeHandle;
public:
    float fA,fB,fC,fD;
    ImGuiPluginUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT),
        fResizeHandle(this)
    {

        // const double scaleFactor = getScaleFactor();

        setSize(DISTRHO_UI_DEFAULT_WIDTH,DISTRHO_UI_DEFAULT_HEIGHT);

        if (isResizable())
            fResizeHandle.hide();


    }

    void stateChanged(const char* key, const char* value){

    }

    ImGuiPluginDSP* getPluginDPSPointer(){
        auto* plugin = static_cast<ImGuiPluginDSP*>(getPluginInstancePointer());
        return plugin;
    }
    bool checkIfClapAtRuntime()
    {
        char fileBuffer[MAX_PATH] = {0};
        HMODULE hModule = NULL;

        // 🟢 Create a dummy static variable. It lives inside your plugin library's binary memory space.
        static const int dummyAnchor = 0;

        // 🟢 Pass the address of the dummy anchor variable instead of the member function pointer
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&dummyAnchor), &hModule);

        if (hModule) {
            GetModuleFileNameA(hModule, fileBuffer, sizeof(fileBuffer));
            std::string binaryPath(fileBuffer);

            std::transform(binaryPath.begin(), binaryPath.end(), binaryPath.begin(), ::tolower);

            std::string target = ".clap";
            if (binaryPath.length() >= target.length()) {
                return (binaryPath.compare(binaryPath.length() - target.length(), target.length(), target) == 0);
            }
        }
        return false; // 🔵 Fallback (VST3, etc.)
    }

    int getPluginFormat()
    {   if(checkIfClapAtRuntime())        return 1;
        return 0;
    }

    void setDirty(){
        //only works in bitwig
        const uint32_t activeFormat = getPluginFormat();

        if (activeFormat == 1)
        {
            auto* clapPointer=reinterpret_cast<const clap_host_t*>(getPluginDPSPointer()->host);
            if(!clapPointer)return;
            auto* hostState = reinterpret_cast<const clap_host_state_t*>(clapPointer->get_extension(clapPointer, CLAP_EXT_STATE));
            if (hostState != nullptr && hostState->mark_dirty != nullptr) {


                hostState->mark_dirty(clapPointer);
            }
        }
    }

    // Window& getWindow() const override {
    //     return UI::getWindow();
    // }



protected:
    void parameterChanged(uint32_t index, float value) override {
        if(index==kParamA){
            fA = value;
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
        repaint();
    }



    void onImGuiDisplay() override {


        const float height = getHeight();
        const float width = getWidth();


        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width , height ));

        if (ImGui::Begin("neural", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar))
        {
        }
        //if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) endDrag();

        ImGui::End();
    }

    ~ImGuiPluginUI(){

    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImGuiPluginUI)
};

// This must stay outside the class because it is a global framework entry point
UI* createUI()
{
    return new ImGuiPluginUI();
}

END_NAMESPACE_DISTRHO
