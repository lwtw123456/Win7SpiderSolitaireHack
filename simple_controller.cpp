#include "simple_controller.h"

#include <windows.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cwchar>

#include "SpiderSolitaireHack.h"

namespace simple_controller
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"SpiderSolitaireHackWindowClass";
        constexpr wchar_t kWindowTitle[] = L"蜘蛛纸牌九项修改器";

        constexpr int kWindowWidth = 360;
        constexpr int kWindowHeight = 486;

        enum class ControlId : int
        {
            FreeMoveOn      = 1001,
            FreeMoveOff     = 1002,
            FreePickOn      = 1003,
            FreePickOff     = 1004,
            FaceUpOn        = 1005,
            FaceUpOff       = 1006,
            OrderedDealOn   = 1007,
            OrderedDealOff  = 1008,
            NoMoveCountOn   = 1009,
            NoMoveCountOff  = 1010,
            NoScoreDedOn    = 1011,
            NoScoreDedOff   = 1012,
            AutoCollectOn   = 1013,
            AutoCollectOff  = 1014,
            WinNow          = 1015,
            SetScore        = 1016,
            SetMoves        = 1017,
            EditScore       = 2001,
            EditMoves       = 2002,
        };

        [[nodiscard]] constexpr int ToInt(ControlId id) noexcept
        {
            return static_cast<int>(id);
        }

        [[nodiscard]] HMENU ToMenuHandle(ControlId id) noexcept
        {
            return reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(ToInt(id)));
        }

        [[nodiscard]] bool TryReadNonNegativeInt(
            HWND owner,
            ControlId editId,
            int& value)
        {
            std::array<wchar_t, 32> buffer{};
            const int length = GetDlgItemTextW(
                owner,
                ToInt(editId),
                buffer.data(),
                static_cast<int>(buffer.size()));

            if (length <= 0)
                return false;

            wchar_t* end = nullptr;
            errno = 0;
            const long parsed = std::wcstol(buffer.data(), &end, 10);

            if (errno == ERANGE ||
                end == buffer.data() ||
                *end != L'\0' ||
                parsed < 0 ||
                parsed > INT_MAX)
            {
                return false;
            }

            value = static_cast<int>(parsed);
            return true;
        }
    }

    class Application final
    {
    public:
        void Run()
        {
            instance_ = GetModuleHandleW(nullptr);
            if (!instance_)
                return;

            WNDCLASSW windowClass{};
            windowClass.lpfnWndProc = &Application::StaticWindowProcedure;
            windowClass.hInstance = instance_;
            windowClass.lpszClassName = kWindowClassName;
            windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            windowClass.hbrBackground =
                reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

            const ATOM classAtom = RegisterClassW(&windowClass);
            if (!classAtom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return;

            const bool ownsWindowClass = classAtom != 0;

            window_ = CreateWindowExW(
                0,
                kWindowClassName,
                kWindowTitle,
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                kWindowWidth,
                kWindowHeight,
                nullptr,
                nullptr,
                instance_,
                this);

            if (!window_)
            {
                if (ownsWindowClass)
                    UnregisterClassW(kWindowClassName, instance_);
                return;
            }

            ShowWindow(window_, SW_SHOW);
            UpdateWindow(window_);

            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            hack_.ResetAll();

            if (ownsWindowClass)
                UnregisterClassW(kWindowClassName, instance_);
        }

    private:
        static LRESULT CALLBACK StaticWindowProcedure(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam)
        {
            Application* application = nullptr;

            if (message == WM_NCCREATE)
            {
                const auto* create =
                    reinterpret_cast<const CREATESTRUCTW*>(lParam);
                application = static_cast<Application*>(create->lpCreateParams);

                if (!application)
                    return FALSE;

                application->window_ = window;
                SetWindowLongPtrW(
                    window,
                    GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(application));
            }
            else
            {
                application = reinterpret_cast<Application*>(
                    GetWindowLongPtrW(window, GWLP_USERDATA));
            }

            if (!application)
                return DefWindowProcW(window, message, wParam, lParam);

            return application->WindowProcedure(
                window,
                message,
                wParam,
                lParam);
        }

        LRESULT WindowProcedure(
            HWND window,
            UINT message,
            WPARAM wParam,
            LPARAM lParam)
        {
            switch (message)
            {
            case WM_CREATE:
                if (!CreateControls(window))
                {
                    ShowMessage(
                        window,
                        L"创建窗口控件失败。",
                        L"初始化失败",
                        MB_ICONERROR);
                    return -1;
                }
                return 0;

            case WM_COMMAND:
                return HandleCommand(window, wParam);

            case WM_DESTROY:
                hack_.ResetAll();
                PostQuitMessage(0);
                return 0;

            case WM_NCDESTROY:
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                window_ = nullptr;
                return DefWindowProcW(window, message, wParam, lParam);

            default:
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }

        LRESULT HandleCommand(HWND owner, WPARAM wParam)
        {
            if (HIWORD(wParam) != BN_CLICKED)
                return 0;

            switch (static_cast<ControlId>(LOWORD(wParam)))
            {
            case ControlId::FreeMoveOn:
                HandleToggle(owner, hack_.EnableFreeMove(), L"自由移牌", ControlId::FreeMoveOn, ControlId::FreeMoveOff, true);
                break;
            case ControlId::FreeMoveOff:
                HandleToggle(owner, hack_.DisableFreeMove(), L"自由移牌", ControlId::FreeMoveOn, ControlId::FreeMoveOff, false);
                break;
            case ControlId::FreePickOn:
                HandleToggle(owner, hack_.EnableFreePick(), L"自由选牌", ControlId::FreePickOn, ControlId::FreePickOff, true);
                break;
            case ControlId::FreePickOff:
                HandleToggle(owner, hack_.DisableFreePick(), L"自由选牌", ControlId::FreePickOn, ControlId::FreePickOff, false);
                break;
            case ControlId::FaceUpOn:
                HandleToggle(owner, hack_.EnableFaceUp(), L"明牌开局", ControlId::FaceUpOn, ControlId::FaceUpOff, true);
                break;
            case ControlId::FaceUpOff:
                HandleToggle(owner, hack_.DisableFaceUp(), L"明牌开局", ControlId::FaceUpOn, ControlId::FaceUpOff, false);
                break;
            case ControlId::OrderedDealOn:
                HandleToggle(owner, hack_.EnableOrderedDeal(), L"有序开局", ControlId::OrderedDealOn, ControlId::OrderedDealOff, true);
                break;
            case ControlId::OrderedDealOff:
                HandleToggle(owner, hack_.DisableOrderedDeal(), L"有序开局", ControlId::OrderedDealOn, ControlId::OrderedDealOff, false);
                break;
            case ControlId::NoMoveCountOn:
                HandleToggle(owner, hack_.EnableNoMoveCount(), L"不加移牌数", ControlId::NoMoveCountOn, ControlId::NoMoveCountOff, true);
                break;
            case ControlId::NoMoveCountOff:
                HandleToggle(owner, hack_.DisableNoMoveCount(), L"不加移牌数", ControlId::NoMoveCountOn, ControlId::NoMoveCountOff, false);
                break;
            case ControlId::NoScoreDedOn:
                HandleToggle(owner, hack_.EnableNoScoreDeduction(), L"移牌不扣分", ControlId::NoScoreDedOn, ControlId::NoScoreDedOff, true);
                break;
            case ControlId::NoScoreDedOff:
                HandleToggle(owner, hack_.DisableNoScoreDeduction(), L"移牌不扣分", ControlId::NoScoreDedOn, ControlId::NoScoreDedOff, false);
                break;
            case ControlId::AutoCollectOn:
                HandleToggle(owner, hack_.EnableAutoCollect(), L"满13张即收", ControlId::AutoCollectOn, ControlId::AutoCollectOff, true);
                break;
            case ControlId::AutoCollectOff:
                HandleToggle(owner, hack_.DisableAutoCollect(), L"满13张即收", ControlId::AutoCollectOn, ControlId::AutoCollectOff, false);
                break;
            case ControlId::WinNow:
                ShowHackResult(owner, hack_.WinNow(), L"直接获胜");
                break;
            case ControlId::SetScore:
                OnSetScore(owner);
                break;
            case ControlId::SetMoves:
                OnSetMoves(owner);
                break;
            default:
                break;
            }

            return 0;
        }

        [[nodiscard]] bool CreateControls(HWND parent) const
        {
            if (!CreateSectionTitle(parent, 10, L"─── 开关功能 ───"))
                return false;

            if (!CreateToggleRow(parent, 38,  L"自由移牌",   ControlId::FreeMoveOn,    ControlId::FreeMoveOff) ||
                !CreateToggleRow(parent, 74,  L"自由选牌",   ControlId::FreePickOn,    ControlId::FreePickOff) ||
                !CreateToggleRow(parent, 110, L"明牌开局",   ControlId::FaceUpOn,      ControlId::FaceUpOff) ||
                !CreateToggleRow(parent, 146, L"有序开局",   ControlId::OrderedDealOn, ControlId::OrderedDealOff) ||
                !CreateToggleRow(parent, 182, L"不加移牌数", ControlId::NoMoveCountOn, ControlId::NoMoveCountOff) ||
                !CreateToggleRow(parent, 218, L"移牌不扣分", ControlId::NoScoreDedOn,  ControlId::NoScoreDedOff) ||
                !CreateToggleRow(parent, 254, L"满13张即收", ControlId::AutoCollectOn, ControlId::AutoCollectOff))
            {
                return false;
            }

            if (!CreateSectionTitle(parent, 294, L"─── 数值修改 ───") ||
                !CreateEditRow(parent, 322, L"分  数：", ControlId::EditScore, ControlId::SetScore) ||
                !CreateEditRow(parent, 358, L"移牌数：", ControlId::EditMoves, ControlId::SetMoves))
            {
                return false;
            }

            return CreateWindowW(
                       L"BUTTON",
                       L"直接获胜",
                       WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                       90,
                       404,
                       160,
                       36,
                       parent,
                       ToMenuHandle(ControlId::WinNow),
                       instance_,
                       nullptr) != nullptr;
        }

        [[nodiscard]] bool CreateSectionTitle(
            HWND parent,
            int y,
            const wchar_t* text) const
        {
            return CreateWindowW(
                       L"STATIC",
                       text,
                       WS_VISIBLE | WS_CHILD | SS_CENTER,
                       15,
                       y,
                       320,
                       20,
                       parent,
                       nullptr,
                       instance_,
                       nullptr) != nullptr;
        }

        [[nodiscard]] bool CreateToggleRow(
            HWND parent,
            int y,
            const wchar_t* label,
            ControlId enableId,
            ControlId disableId) const
        {
            const HWND labelWindow = CreateWindowW(
                L"STATIC",
                label,
                WS_VISIBLE | WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
                15,
                y,
                160,
                28,
                parent,
                nullptr,
                instance_,
                nullptr);

            const HWND enableButton = CreateWindowW(
                L"BUTTON",
                L"开启",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON | BS_PUSHLIKE,
                185,
                y,
                70,
                28,
                parent,
                ToMenuHandle(enableId),
                instance_,
                nullptr);

            const HWND disableButton = CreateWindowW(
                L"BUTTON",
                L"关闭",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON | BS_PUSHLIKE,
                265,
                y,
                70,
                28,
                parent,
                ToMenuHandle(disableId),
                instance_,
                nullptr);

            if (!labelWindow || !enableButton || !disableButton)
                return false;

            SendMessageW(disableButton, BM_SETCHECK, BST_CHECKED, 0);
            return true;
        }

        static void SetToggleSelection(
            HWND owner,
            ControlId enableId,
            ControlId disableId,
            bool enabled)
        {
            SendDlgItemMessageW(owner, ToInt(enableId), BM_SETCHECK,
                enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            SendDlgItemMessageW(owner, ToInt(disableId), BM_SETCHECK,
                enabled ? BST_UNCHECKED : BST_CHECKED, 0);
        }

        static void HandleToggle(
            HWND owner,
            spider_solitaire::HackResult result,
            const wchar_t* featureName,
            ControlId enableId,
            ControlId disableId,
            bool enabled)
        {
            using spider_solitaire::HackResult;

            const bool confirmed = enabled
                ? result == HackResult::Success || result == HackResult::AlreadyEnabled
                : result == HackResult::Success ||
                  result == HackResult::AlreadyDisabled ||
                  result == HackResult::NotInstalled;

            if (confirmed)
                SetToggleSelection(owner, enableId, disableId, enabled);

            ShowHackResult(owner, result, featureName);
        }

        [[nodiscard]] bool CreateEditRow(
            HWND parent,
            int y,
            const wchar_t* label,
            ControlId editId,
            ControlId buttonId) const
        {
            const HWND labelWindow = CreateWindowW(
                L"STATIC",
                label,
                WS_VISIBLE | WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
                15,
                y,
                80,
                28,
                parent,
                nullptr,
                instance_,
                nullptr);

            const HWND editWindow = CreateWindowW(
                L"EDIT",
                L"",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
                100,
                y,
                150,
                28,
                parent,
                ToMenuHandle(editId),
                instance_,
                nullptr);

            const HWND modifyButton = CreateWindowW(
                L"BUTTON",
                L"修改",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                265,
                y,
                70,
                28,
                parent,
                ToMenuHandle(buttonId),
                instance_,
                nullptr);

            return labelWindow && editWindow && modifyButton;
        }

        static void ShowMessage(
            HWND owner,
            const wchar_t* text,
            const wchar_t* title,
            UINT icon)
        {
            MessageBoxW(owner, text, title, MB_OK | icon);
        }

        static void ShowHackResult(
            HWND owner,
            spider_solitaire::HackResult result,
            const wchar_t* featureName)
        {
            using spider_solitaire::HackResult;

            std::array<wchar_t, 160> message{};
            const wchar_t* title = L"提示";
            UINT icon = MB_ICONINFORMATION;

            switch (result)
            {
            case HackResult::Success:
                return;
            case HackResult::AlreadyEnabled:
                std::swprintf(message.data(), message.size(), L"%ls已经启用。", featureName);
                break;
            case HackResult::AlreadyDisabled:
                std::swprintf(message.data(), message.size(), L"%ls已经关闭。", featureName);
                break;
            case HackResult::NotInstalled:
                std::swprintf(message.data(), message.size(), L"%ls尚未启用。", featureName);
                break;
            case HackResult::NotImplemented:
                std::swprintf(message.data(), message.size(), L"%ls功能尚未实现。", featureName);
                break;
            case HackResult::InvalidValue:
                std::swprintf(message.data(), message.size(), L"%ls输入值无效。", featureName);
                title = L"输入错误";
                icon = MB_ICONWARNING;
                break;
            case HackResult::PatternNotFound:
                std::swprintf(message.data(), message.size(), L"未找到%ls所需的目标特征码。", featureName);
                title = L"执行失败";
                icon = MB_ICONERROR;
                break;
            case HackResult::PatchWriteFailed:
                std::swprintf(message.data(), message.size(), L"写入%ls Patch 失败。", featureName);
                title = L"执行失败";
                icon = MB_ICONERROR;
                break;
            case HackResult::PatchEnableFailed:
                std::swprintf(message.data(), message.size(), L"重新启用%ls Patch 失败。", featureName);
                title = L"执行失败";
                icon = MB_ICONERROR;
                break;
            case HackResult::PatchDisableFailed:
                std::swprintf(message.data(), message.size(), L"恢复%ls原始字节失败。", featureName);
                title = L"执行失败";
                icon = MB_ICONERROR;
                break;
			case HackResult::PointerChainFailed:
				std::swprintf(message.data(), message.size(), L"无法定位%ls所需的内存地址。请确认游戏已经进入牌局。", featureName);
				title = L"执行失败";
				icon = MB_ICONERROR;
				break;
			case HackResult::MemoryWriteFailed:
				std::swprintf(message.data(), message.size(), L"写入%ls数据失败。", featureName);
				title = L"执行失败";
				icon = MB_ICONERROR;
				break;
            }

            ShowMessage(owner, message.data(), title, icon);
        }

        void OnSetScore(HWND owner)
        {
            int score = 0;
            if (!TryReadNonNegativeInt(owner, ControlId::EditScore, score))
            {
                ShowMessage(
                    owner,
                    L"请输入有效的非负整数分数。",
                    L"输入错误",
                    MB_ICONWARNING);
                return;
            }

            ShowHackResult(owner, hack_.SetScore(score), L"修改分数");
        }

        void OnSetMoves(HWND owner)
        {
            int moves = 0;
            if (!TryReadNonNegativeInt(owner, ControlId::EditMoves, moves))
            {
                ShowMessage(
                    owner,
                    L"请输入有效的非负整数移牌数。",
                    L"输入错误",
                    MB_ICONWARNING);
                return;
            }

            ShowHackResult(owner, hack_.SetMoves(moves), L"修改移牌数");
        }

        HINSTANCE instance_{};
        HWND window_{};
        spider_solitaire::SpiderSolitaireHack hack_{};
    };
}

extern "C" void RunWindow(void)
{
    simple_controller::Application application;
    application.Run();
}
