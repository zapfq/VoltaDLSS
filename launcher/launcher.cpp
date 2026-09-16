#define NOMINMAX

#include <windows.h>
#include <commdlg.h>
#include <tlhelp32.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <cwctype>

#pragma comment(lib, "comdlg32.lib")

namespace
{
    constexpr COLORREF kBackground =
        RGB(8, 8, 10);

    constexpr COLORREF kPanel =
        RGB(16, 16, 19);

    constexpr COLORREF kGold =
        RGB(212, 175, 55);

    constexpr COLORREF kGoldDark =
        RGB(105, 85, 24);

    constexpr COLORREF kWhite =
        RGB(245, 245, 245);

    constexpr COLORREF kGrey =
        RGB(155, 155, 160);

    constexpr COLORREF kGreen =
        RGB(90, 220, 120);


    constexpr int IDC_WINDOW_SELECT = 1001;
    constexpr int IDC_BROWSE = 1002;

    constexpr int IDC_QUALITY_UP = 1010;
    constexpr int IDC_QUALITY_PERF = 1011;
    constexpr int IDC_QUALITY_BAL = 1012;
    constexpr int IDC_QUALITY_QUALITY = 1013;
    constexpr int IDC_QUALITY_NATIVE = 1014;

    constexpr int IDC_INPUT = 1020;
    constexpr int IDC_START = 1030;


    HWND gWindow = nullptr;
    HWND gWindowSelect = nullptr;
    HWND gInputCheck = nullptr;

    std::array<HWND, 5> gQualityButtons{};

    HFONT gTitleFont = nullptr;
    HFONT gSubtitleFont = nullptr;
    HFONT gSectionFont = nullptr;
    HFONT gNormalFont = nullptr;
    HFONT gButtonFont = nullptr;
    HFONT gSmallFont = nullptr;

    HBRUSH gBackgroundBrush = nullptr;
    HBRUSH gPanelBrush = nullptr;

    std::wstring gSelectedQuality =
        L"quality";

    std::wstring gCustomWindowPath;


    HMENU controlMenuId(
        int id)
    {
        return reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(id));
    }


    bool iequals(
        const std::wstring& a,
        const std::wstring& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
        {
            if (towlower(a[i]) !=
                towlower(b[i]))
            {
                return false;
            }
        }

        return true;
    }


    DWORD findProcess(
        const std::wstring& processName,
        std::wstring* executablePath)
    {
        HANDLE snapshot =
            CreateToolhelp32Snapshot(
                TH32CS_SNAPPROCESS,
                0);

        if (snapshot ==
            INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize =
            sizeof(entry);

        if (!Process32FirstW(
                snapshot,
                &entry))
        {
            CloseHandle(snapshot);
            return 0;
        }

        do
        {
            if (iequals(
                    entry.szExeFile,
                    processName))
            {
                HANDLE process =
                    OpenProcess(
                        PROCESS_QUERY_LIMITED_INFORMATION,
                        FALSE,
                        entry.th32ProcessID);

                if (process &&
                    executablePath)
                {
                    wchar_t path[MAX_PATH * 4]{};
                    DWORD size =
                        ARRAYSIZE(path);

                    if (QueryFullProcessImageNameW(
                            process,
                            0,
                            path,
                            &size))
                    {
                        *executablePath =
                            std::wstring(
                                path,
                                size);
                    }

                    CloseHandle(process);
                }

                DWORD pid =
                    entry.th32ProcessID;

                CloseHandle(snapshot);

                return pid;
            }

        } while (
            Process32NextW(
                snapshot,
                &entry));

        CloseHandle(snapshot);

        return 0;
    }


    std::wstring browseForExe()
    {
        wchar_t fileName[MAX_PATH * 4]{};

        OPENFILENAMEW ofn{};
        ofn.lStructSize =
            sizeof(ofn);

        ofn.hwndOwner =
            gWindow;

        ofn.lpstrFilter =
            L"Applications (*.exe)\0*.exe\0"
            L"All Files (*.*)\0*.*\0";

        ofn.lpstrFile =
            fileName;

        ofn.nMaxFile =
            ARRAYSIZE(fileName);

        ofn.Flags =
            OFN_FILEMUSTEXIST |
            OFN_PATHMUSTEXIST;

        ofn.lpstrTitle =
            L"Select Window Application";

        if (!GetOpenFileNameW(
                &ofn))
        {
            return L"";
        }

        return fileName;
    }


    std::filesystem::path projectRoot()
    {
        wchar_t buffer[MAX_PATH * 4]{};

        DWORD length =
            GetModuleFileNameW(
                nullptr,
                buffer,
                ARRAYSIZE(buffer));

        if (length == 0)
            return {};

        std::filesystem::path exeDirectory =
            std::filesystem::path(
                std::wstring(
                    buffer,
                    length))
            .parent_path();

        /*
            Development layout:

                VoltaDLSS/
                    build/
                        Release/
                            VoltaDLSS.exe
                            VoltaDLSSRuntime.exe

            Find the project root from the launcher location.
        */

        if (exeDirectory.filename() == L"Release" &&
            exeDirectory.parent_path().filename() == L"build")
        {
            return exeDirectory
                .parent_path()
                .parent_path();
        }

        /*
            Packaged layout:

                VoltaDLSS/
                    VoltaDLSS.exe
                    bin/
                        VoltaDLSSRuntime.exe
        */

        return exeDirectory;
    }


    void setEnvironment(
        const wchar_t* name,
        const std::wstring& value)
    {
        SetEnvironmentVariableW(
            name,
            value.c_str());
    }


    void clearEnvironment(
        const wchar_t* name)
    {
        SetEnvironmentVariableW(
            name,
            nullptr);
    }


    bool launchApplication(
        const std::wstring& executable,
        PROCESS_INFORMATION& pi)
    {
        STARTUPINFOW si{};
        si.cb =
            sizeof(si);

        std::wstring commandLine =
            L"\"" +
            executable +
            L"\"";

        std::vector<wchar_t> command(
            commandLine.begin(),
            commandLine.end());

        command.push_back(
            L'\0');

        const std::filesystem::path workingDirectory =
            std::filesystem::path(
                executable)
                .parent_path();

        return CreateProcessW(
            executable.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            workingDirectory.empty()
                ? nullptr
                : workingDirectory.c_str(),
            &si,
            &pi) != FALSE;
    }


    void refreshWindowSelector()
    {
        if (!gWindowSelect)
            return;

        SendMessageW(
            gWindowSelect,
            CB_RESETCONTENT,
            0,
            0);

        struct KnownWindow
        {
            const wchar_t* displayName;
            const wchar_t* processName;
        };

        constexpr KnownWindow known[] =
        {
            {
                L"Cyberpunk 2077",
                L"Cyberpunk2077.exe"
            },
            {
                L"Forza Horizon 5",
                L"ForzaHorizon5.exe"
            }
        };

        int runningCount = 0;
        int selectedIndex = -1;

        for (const auto& item : known)
        {
            std::wstring path;

            DWORD pid =
                findProcess(
                    item.processName,
                    &path);

            if (pid == 0)
                continue;

            std::wstring label =
                std::wstring(
                    item.displayName) +
                L"  [RUNNING • PID " +
                std::to_wstring(pid) +
                L"]";

            int index =
                static_cast<int>(
                    SendMessageW(
                        gWindowSelect,
                        CB_ADDSTRING,
                        0,
                        reinterpret_cast<LPARAM>(
                            label.c_str())));

            SendMessageW(
                gWindowSelect,
                CB_SETITEMDATA,
                index,
                static_cast<LPARAM>(pid));

            if (selectedIndex < 0)
                selectedIndex = index;

            ++runningCount;
        }

        if (runningCount == 0)
        {
            int index =
                static_cast<int>(
                    SendMessageW(
                        gWindowSelect,
                        CB_ADDSTRING,
                        0,
                        reinterpret_cast<LPARAM>(
                            L"None")));

            SendMessageW(
                gWindowSelect,
                CB_SETITEMDATA,
                index,
                0);

            selectedIndex =
                index;
        }

        SendMessageW(
            gWindowSelect,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(
                L"Browse for window EXE..."));

        if (!gCustomWindowPath.empty())
        {
            selectedIndex =
                static_cast<int>(
                    SendMessageW(
                        gWindowSelect,
                        CB_ADDSTRING,
                        0,
                        reinterpret_cast<LPARAM>(
                            L"Custom window")));
        }

        SendMessageW(
            gWindowSelect,
            CB_SETCURSEL,
            selectedIndex < 0
                ? 0
                : selectedIndex,
            0);
    }


    bool resolveSelectedWindow(
        DWORD& pid)
    {
        pid = 0;

        int selection =
            static_cast<int>(
                SendMessageW(
                    gWindowSelect,
                    CB_GETCURSEL,
                    0,
                    0));

        if (selection < 0)
            return false;

        pid =
            static_cast<DWORD>(
                SendMessageW(
                    gWindowSelect,
                    CB_GETITEMDATA,
                    selection,
                    0));

        if (pid != 0)
            return true;

        wchar_t label[512]{};

        SendMessageW(
            gWindowSelect,
            CB_GETLBTEXT,
            selection,
            reinterpret_cast<LPARAM>(
                label));

        const std::wstring selected =
            label;

        if (selected ==
            L"Browse for window EXE...")
        {
            gCustomWindowPath =
                browseForExe();

            return !gCustomWindowPath.empty();
        }

        if (selected ==
            L"Custom window" &&
            !gCustomWindowPath.empty())
        {
            return true;
        }

        return false;
    }


    const wchar_t* qualityLabel(
        int id)
    {
        switch (id)
        {
        case IDC_QUALITY_UP:
            return L"ULTRA PERFORMANCE";

        case IDC_QUALITY_PERF:
            return L"PERFORMANCE";

        case IDC_QUALITY_BAL:
            return L"BALANCED";

        case IDC_QUALITY_QUALITY:
            return L"QUALITY";

        case IDC_QUALITY_NATIVE:
            return L"NATIVE";
        }

        return L"";
    }


    bool qualitySelected(
        int id)
    {
        switch (id)
        {
        case IDC_QUALITY_UP:
            return gSelectedQuality ==
                L"ultraperformance";

        case IDC_QUALITY_PERF:
            return gSelectedQuality ==
                L"performance";

        case IDC_QUALITY_BAL:
            return gSelectedQuality ==
                L"balanced";

        case IDC_QUALITY_QUALITY:
            return gSelectedQuality ==
                L"quality";

        case IDC_QUALITY_NATIVE:
            return gSelectedQuality ==
                L"native";
        }

        return false;
    }


    void selectQuality(
        const std::wstring& quality)
    {
        gSelectedQuality =
            quality;

        for (HWND button :
             gQualityButtons)
        {
            if (button)
            {
                InvalidateRect(
                    button,
                    nullptr,
                    TRUE);
            }
        }
    }


    bool startVoltaDLSS()
    {
        DWORD targetPid = 0;

        if (!resolveSelectedWindow(
                targetPid))
        {
            MessageBoxW(
                gWindow,
                L"Please select a running window "
                L"or browse for an EXE.",
                L"VoltaDLSS",
                MB_ICONWARNING);

            return false;
        }

        const std::filesystem::path root =
            projectRoot();

        const std::filesystem::path runtime =
            root /
            L"build" /
            L"Release" /
            L"VoltaDLSSRuntime.exe";

        if (!std::filesystem::exists(
                runtime))
        {
            MessageBoxW(
                gWindow,
                L"VoltaDLSSRuntime.exe was not found.\n\n"
                L"Expected:\n"
                L"bin\\VoltaDLSSRuntime.exe",
                L"VoltaDLSS",
                MB_ICONERROR);

            return false;
        }

        PROCESS_INFORMATION gameProcess{};

        /*
            If a custom EXE was selected and isn't already
            running, launch it first.
        */

        if (targetPid == 0)
        {
            if (gCustomWindowPath.empty())
            {
                MessageBoxW(
                    gWindow,
                    L"No running window is selected.",
                    L"VoltaDLSS",
                    MB_ICONWARNING);

                return false;
            }

            if (!launchApplication(
                    gCustomWindowPath,
                    gameProcess))
            {
                MessageBoxW(
                    gWindow,
                    L"Failed to launch the selected application.",
                    L"VoltaDLSS",
                    MB_ICONERROR);

                return false;
            }

            targetPid =
                gameProcess.dwProcessId;

            CloseHandle(
                gameProcess.hThread);

            CloseHandle(
                gameProcess.hProcess);

            Sleep(1200);
        }

        setEnvironment(
            L"VOLTADLSS_TARGET",
            std::to_wstring(targetPid));

        setEnvironment(
            L"VOLTADLSS_QUALITY",
            gSelectedQuality);

        const bool inputEnabled =
            SendMessageW(
                gInputCheck,
                BM_GETCHECK,
                0,
                0) ==
            BST_CHECKED;

        const std::filesystem::path ucr =
            root /
            L"third_party" /
            L"UCR" /
            L"UCR.exe";

        if (inputEnabled &&
            std::filesystem::exists(ucr))
        {
            setEnvironment(
                L"VOLTADLSS_UCR",
                ucr.wstring());
        }
        else
        {
            clearEnvironment(
                L"VOLTADLSS_UCR");
        }

        STARTUPINFOW si{};
        si.cb =
            sizeof(si);

        PROCESS_INFORMATION runtimeProcess{};

        std::wstring commandLine =
            L"\"" +
            runtime.wstring() +
            L"\"";

        std::vector<wchar_t> command(
            commandLine.begin(),
            commandLine.end());

        command.push_back(
            L'\0');

        const BOOL result =
            CreateProcessW(
                runtime.c_str(),
                command.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                root.c_str(),
                &si,
                &runtimeProcess);

        if (!result)
        {
            MessageBoxW(
                gWindow,
                L"Failed to start VoltaDLSSRuntime.exe.",
                L"VoltaDLSS",
                MB_ICONERROR);

            return false;
        }

        CloseHandle(
            runtimeProcess.hThread);

        CloseHandle(
            runtimeProcess.hProcess);

        DestroyWindow(
            gWindow);

        return true;
    }


    void drawText(
        HDC dc,
        HFONT font,
        COLORREF color,
        int x,
        int y,
        const wchar_t* text)
    {
        HFONT oldFont =
            static_cast<HFONT>(
                SelectObject(
                    dc,
                    font));

        SetTextColor(
            dc,
            color);

        SetBkMode(
            dc,
            TRANSPARENT);

        TextOutW(
            dc,
            x,
            y,
            text,
            static_cast<int>(
                wcslen(text)));

        SelectObject(
            dc,
            oldFont);
    }


    LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            gWindow =
                hwnd;

            gBackgroundBrush =
                CreateSolidBrush(
                    kBackground);

            gPanelBrush =
                CreateSolidBrush(
                    kPanel);

            gTitleFont =
                CreateFontW(
                    30,
                    0,
                    0,
                    0,
                    FW_BOLD,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH,
                    L"Segoe UI");

            gSubtitleFont =
                CreateFontW(
                    13,
                    0,
                    0,
                    0,
                    FW_NORMAL,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH,
                    L"Segoe UI");

            gSectionFont =
                CreateFontW(
                    16,
                    0,
                    0,
                    0,
                    FW_BOLD,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH,
                    L"Segoe UI");

            gNormalFont =
                CreateFontW(
                    14,
                    0,
                    0,
                    0,
                    FW_NORMAL,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH,
                    L"Segoe UI");

            gButtonFont =
                CreateFontW(
                    12,
                    0,
                    0,
                    0,
                    FW_BOLD,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH,
                    L"Segoe UI");

            gSmallFont =
                CreateFontW(
                    11,
                    0,
                    0,
                    0,
                    FW_NORMAL,
                    FALSE,
                    FALSE,
                    FALSE,
                    DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY,
                    DEFAULT_PITCH,
                    L"Segoe UI");


            /*
                WINDOW
            */

            gWindowSelect =
                CreateWindowExW(
                    0,
                    L"COMBOBOX",
                    nullptr,
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    CBS_DROPDOWNLIST |
                    WS_VSCROLL,
                    32,
                    118,
                    536,
                    34,
                    hwnd,
                    controlMenuId(
                        IDC_WINDOW_SELECT),
                    nullptr,
                    nullptr);

            SendMessageW(
                gWindowSelect,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(
                    gNormalFont),
                TRUE);

            refreshWindowSelector();


            CreateWindowExW(
                0,
                L"BUTTON",
                L"Browse...",
                WS_CHILD |
                WS_VISIBLE |
                WS_TABSTOP |
                BS_PUSHBUTTON |
                BS_OWNERDRAW,
                472,
                160,
                96,
                34,
                hwnd,
                controlMenuId(
                    IDC_BROWSE),
                nullptr,
                nullptr);


            /*
                QUALITY

                [ ULTRA PERFORMANCE ] [ PERFORMANCE ]
                [ BALANCED           ] [ QUALITY     ]
                [                 NATIVE             ]
            */

            const int buttonW = 264;
            const int buttonH = 44;
            const int gap = 8;

            const int leftX = 32;
            const int rightX =
                leftX +
                buttonW +
                gap;

            const int row1Y = 236;
            const int row2Y = 288;
            const int nativeY = 340;

            struct QualityPlacement
            {
                int id;
                int x;
                int y;
                int width;
            };

            constexpr QualityPlacement placements[] =
            {
                {
                    IDC_QUALITY_UP,
                    leftX,
                    row1Y,
                    buttonW
                },
                {
                    IDC_QUALITY_PERF,
                    rightX,
                    row1Y,
                    buttonW
                },
                {
                    IDC_QUALITY_BAL,
                    leftX,
                    row2Y,
                    buttonW
                },
                {
                    IDC_QUALITY_QUALITY,
                    rightX,
                    row2Y,
                    buttonW
                },
                {
                    IDC_QUALITY_NATIVE,
                    leftX,
                    nativeY,
                    buttonW * 2 + gap
                }
            };

            for (int i = 0;
                 i < 5;
                 ++i)
            {
                gQualityButtons[i] =
                    CreateWindowExW(
                        0,
                        L"BUTTON",
                        qualityLabel(
                            placements[i].id),
                        WS_CHILD |
                        WS_VISIBLE |
                        WS_TABSTOP |
                        BS_PUSHBUTTON |
                        BS_OWNERDRAW,
                        placements[i].x,
                        placements[i].y,
                        placements[i].width,
                        buttonH,
                        hwnd,
                        controlMenuId(
                            placements[i].id),
                        nullptr,
                        nullptr);

                SendMessageW(
                    gQualityButtons[i],
                    WM_SETFONT,
                    reinterpret_cast<WPARAM>(
                        gButtonFont),
                    TRUE);
            }

            selectQuality(
                L"quality");


            /*
                INPUT
            */

            CreateWindowExW(
                0,
                L"STATIC",
                L"INPUT",
                WS_CHILD |
                WS_VISIBLE,
                32,
                400,
                100,
                24,
                hwnd,
                nullptr,
                nullptr,
                nullptr);

            gInputCheck =
                CreateWindowExW(
                    0,
                    L"BUTTON",
                    L"Keyboard + Mouse remapping",
                    WS_CHILD |
                    WS_VISIBLE |
                    WS_TABSTOP |
                    BS_AUTOCHECKBOX,
                    32,
                    424,
                    300,
                    28,
                    hwnd,
                    controlMenuId(
                        IDC_INPUT),
                    nullptr,
                    nullptr);

            SendMessageW(
                gInputCheck,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(
                    gNormalFont),
                TRUE);

            SendMessageW(
                gInputCheck,
                BM_SETCHECK,
                BST_UNCHECKED,
                0);


            /*
                START
            */

            CreateWindowExW(
                0,
                L"BUTTON",
                L"START VOLTADLSS",
                WS_CHILD |
                WS_VISIBLE |
                WS_TABSTOP |
                BS_PUSHBUTTON |
                BS_OWNERDRAW,
                32,
                472,
                536,
                56,
                hwnd,
                controlMenuId(
                    IDC_START),
                nullptr,
                nullptr);


            SetTimer(
                hwnd,
                1,
                1000,
                nullptr);

            return 0;
        }


        case WM_TIMER:
        {
            if (wParam == 1)
            {
                refreshWindowSelector();
            }

            return 0;
        }


        case WM_COMMAND:
        {
            const int id =
                LOWORD(wParam);

            switch (id)
            {
            case IDC_BROWSE:
            {
                std::wstring path =
                    browseForExe();

                if (!path.empty())
                {
                    gCustomWindowPath =
                        path;

                    refreshWindowSelector();

                    int count =
                        static_cast<int>(
                            SendMessageW(
                                gWindowSelect,
                                CB_GETCOUNT,
                                0,
                                0));

                    if (count > 0)
                    {
                        SendMessageW(
                            gWindowSelect,
                            CB_SETCURSEL,
                            count - 1,
                            0);
                    }
                }

                return 0;
            }

            case IDC_QUALITY_UP:
                selectQuality(
                    L"ultraperformance");
                return 0;

            case IDC_QUALITY_PERF:
                selectQuality(
                    L"performance");
                return 0;

            case IDC_QUALITY_BAL:
                selectQuality(
                    L"balanced");
                return 0;

            case IDC_QUALITY_QUALITY:
                selectQuality(
                    L"quality");
                return 0;

            case IDC_QUALITY_NATIVE:
                selectQuality(
                    L"native");
                return 0;

            case IDC_START:
                startVoltaDLSS();
                return 0;
            }

            break;
        }


        case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* dis =
                reinterpret_cast<DRAWITEMSTRUCT*>(
                    lParam);

            if (!dis)
                break;

            const int id =
                static_cast<int>(
                    dis->CtlID);

            const bool selected =
                qualitySelected(id);

            const bool start =
                id == IDC_START;

            const bool browse =
                id == IDC_BROWSE;

            COLORREF fillColor =
                selected || start
                    ? kGold
                    : kPanel;

            HBRUSH fillBrush =
                CreateSolidBrush(
                    fillColor);

            FillRect(
                dis->hDC,
                &dis->rcItem,
                fillBrush);

            DeleteObject(
                fillBrush);

            COLORREF textColor =
                selected || start
                    ? RGB(5, 5, 5)
                    : kWhite;

            if (browse)
                textColor = kWhite;

            HFONT font =
                start
                    ? gSectionFont
                    : gButtonFont;

            HFONT oldFont =
                static_cast<HFONT>(
                    SelectObject(
                        dis->hDC,
                        font));

            SetTextColor(
                dis->hDC,
                textColor);

            SetBkMode(
                dis->hDC,
                TRANSPARENT);

            wchar_t text[256]{};

            GetWindowTextW(
                dis->hwndItem,
                text,
                ARRAYSIZE(text));

            RECT textRect =
                dis->rcItem;

            DrawTextW(
                dis->hDC,
                text,
                -1,
                &textRect,
                DT_CENTER |
                DT_VCENTER |
                DT_SINGLELINE);

            SelectObject(
                dis->hDC,
                oldFont);


            HPEN pen =
                CreatePen(
                    PS_SOLID,
                    1,
                    selected || start
                        ? kGold
                        : kGoldDark);

            HPEN oldPen =
                static_cast<HPEN>(
                    SelectObject(
                        dis->hDC,
                        pen));

            HBRUSH oldBrush =
                static_cast<HBRUSH>(
                    SelectObject(
                        dis->hDC,
                        GetStockObject(
                            HOLLOW_BRUSH)));

            Rectangle(
                dis->hDC,
                dis->rcItem.left,
                dis->rcItem.top,
                dis->rcItem.right - 1,
                dis->rcItem.bottom - 1);

            SelectObject(
                dis->hDC,
                oldBrush);

            SelectObject(
                dis->hDC,
                oldPen);

            DeleteObject(
                pen);

            return TRUE;
        }


        case WM_CTLCOLORSTATIC:
        {
            HDC dc =
                reinterpret_cast<HDC>(
                    wParam);

            SetTextColor(
                dc,
                kWhite);

            SetBkColor(
                dc,
                kBackground);

            SetBkMode(
                dc,
                TRANSPARENT);

            return reinterpret_cast<LRESULT>(
                gBackgroundBrush);
        }


        case WM_CTLCOLORBTN:
        {
            HDC dc =
                reinterpret_cast<HDC>(
                    wParam);

            SetTextColor(
                dc,
                kWhite);

            SetBkColor(
                dc,
                kBackground);

            return reinterpret_cast<LRESULT>(
                gBackgroundBrush);
        }


        case WM_PAINT:
        {
            PAINTSTRUCT ps{};

            HDC dc =
                BeginPaint(
                    hwnd,
                    &ps);

            RECT client{};
            GetClientRect(
                hwnd,
                &client);

            FillRect(
                dc,
                &client,
                gBackgroundBrush);

            /*
                Gold top rule.
            */

            RECT goldLine{
                0,
                0,
                client.right,
                4
            };

            HBRUSH goldBrush =
                CreateSolidBrush(
                    kGold);

            FillRect(
                dc,
                &goldLine,
                goldBrush);

            DeleteObject(
                goldBrush);


            /*
                HEADER
            */

            drawText(
                dc,
                gTitleFont,
                kGold,
                32,
                26,
                L"VoltaDLSS");

            drawText(
                dc,
                gSubtitleFont,
                kGrey,
                34,
                65,
                L"Volta Tensor Upscaler");


            /*
                WINDOW
            */

            drawText(
                dc,
                gSectionFont,
                kWhite,
                32,
                91,
                L"WINDOW");


            /*
                QUALITY
            */

            drawText(
                dc,
                gSectionFont,
                kWhite,
                32,
                207,
                L"QUALITY");


            /*
                INPUT
            */

            drawText(
                dc,
                gSectionFont,
                kWhite,
                32,
                400,
                L"INPUT");


            /*
                STATUS
            */

            RECT status{
                32,
                544,
                client.right - 32,
                588
            };

            FillRect(
                dc,
                &status,
                gPanelBrush);

            drawText(
                dc,
                gSmallFont,
                kGreen,
                44,
                558,
                L"● TITAN V DETECTED");

            drawText(
                dc,
                gSmallFont,
                kGrey,
                190,
                558,
                L"CUDA 12.9");

            drawText(
                dc,
                gSmallFont,
                kGrey,
                285,
                558,
                L"SM 7.0");

            drawText(
                dc,
                gSmallFont,
                kGold,
                380,
                558,
                L"TENSOR CORE");

            drawText(
                dc,
                gSmallFont,
                kGreen,
                468,
                558,
                L"READY");

            EndPaint(
                hwnd,
                &ps);

            return 0;
        }


        case WM_DESTROY:
        {
            KillTimer(
                hwnd,
                1);

            DeleteObject(
                gTitleFont);

            DeleteObject(
                gSubtitleFont);

            DeleteObject(
                gSectionFont);

            DeleteObject(
                gNormalFont);

            DeleteObject(
                gButtonFont);

            DeleteObject(
                gSmallFont);

            DeleteObject(
                gBackgroundBrush);

            DeleteObject(
                gPanelBrush);

            PostQuitMessage(0);

            return 0;
        }
        }

        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }
}


int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    const wchar_t className[] =
        L"VoltaDLSSLauncherClass";

    WNDCLASSW wc{};

    wc.lpfnWndProc =
        WindowProc;

    wc.hInstance =
        instance;

    wc.lpszClassName =
        className;

    wc.hCursor =
        LoadCursorW(
            nullptr,
            MAKEINTRESOURCEW(
                IDC_ARROW));

    wc.hbrBackground =
        CreateSolidBrush(
            kBackground);

    if (!RegisterClassW(
            &wc))
    {
        return 1;
    }

    HWND window =
        CreateWindowExW(
            0,
            className,
            L"VoltaDLSS",
            WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU |
            WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            620,
            650,
            nullptr,
            nullptr,
            instance,
            nullptr);

    if (!window)
        return 1;

    ShowWindow(
        window,
        SW_SHOW);

    UpdateWindow(
        window);

    MSG message{};

    while (
        GetMessageW(
            &message,
            nullptr,
            0,
            0) > 0)
    {
        TranslateMessage(
            &message);

        DispatchMessageW(
            &message);
    }

    return static_cast<int>(
        message.wParam);
}

