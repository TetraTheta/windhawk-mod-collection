// ==WindhawkMod==
// @id              modernize-folder-picker-dialog
// @name            Modernize Folder Picker Dialog
// @description     Replaces compatible classic folder picker dialogs
// @version         0.1.0
// @author          TetraTheta
// @github          https://github.com/TetraTheta
// @include         *
// @compilerOptions -lole32 -loleaut32 -lshell32
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Modernize Folder Picker Dialog

Replaces compatible "Browse For Folder" dialogs with the modern folder picker.
Callback-based installers such as NSIS keep their initial selection and install
path validation. Calls with unsupported options or a restricted folder tree use
the original Windows dialog.
*/
// ==/WindhawkModReadme==

#include <initguid.h>
#include <shlobj.h>
#include <unknwn.h>
#include <windhawk_utils.h>
#include <winrt/base.h>

using SHBrowseForFolderW_t = decltype(&SHBrowseForFolderW);
SHBrowseForFolderW_t SHBrowseForFolderW_orig = nullptr;

struct CallbackBridge {
  LPBROWSEINFOW browseInfo;
  IFileOpenDialog* dialog;
  WNDPROC previousProc;
  HWND window;
  bool allowSelection = true;
  bool unsupported = false;
};

LRESULT CALLBACK CallbackWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* bridge = reinterpret_cast<CallbackBridge*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!bridge) return DefWindowProcW(window, message, wParam, lParam);

  if (message == BFFM_ENABLEOK) {
    bridge->allowSelection = lParam != 0;
    return 0;
  }

  if (message == BFFM_SETSELECTIONW) {
    winrt::com_ptr<IShellItem> folder;
    HRESULT hr = wParam
                   ? SHCreateItemFromParsingName(reinterpret_cast<PCWSTR>(lParam), nullptr, IID_PPV_ARGS(folder.put()))
                   : SHCreateItemFromIDList(reinterpret_cast<PCIDLIST_ABSOLUTE>(lParam), IID_PPV_ARGS(folder.put()));
    if (SUCCEEDED(hr)) bridge->dialog->SetFolder(folder.get());
    return 0;
  }

  if (message == BFFM_SETOKTEXT) {
    bridge->dialog->SetOkButtonLabel(reinterpret_cast<PCWSTR>(lParam));
    return 0;
  }

  if (message >= WM_USER) {
    // shortcut: only common browse messages are bridged; extend this for callbacks needing more.
    bridge->unsupported = true;
    return 0;
  }

  return CallWindowProcW(bridge->previousProc, window, message, wParam, lParam);
}

struct DialogEvents : winrt::implements<DialogEvents, IFileDialogEvents> {
  explicit DialogEvents(CallbackBridge* bridge) : bridge(bridge) {}

  HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog* dialog) override {
    winrt::com_ptr<IShellItem> selected;
    if (FAILED(dialog->GetResult(selected.put()))) return S_FALSE;

    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetIDListFromObject(selected.get(), &pidl))) return S_FALSE;

    bridge->allowSelection = true;
    bridge->browseInfo->lpfn(bridge->window, BFFM_SELCHANGED, reinterpret_cast<LPARAM>(pidl),
                             bridge->browseInfo->lParam);
    CoTaskMemFree(pidl);
    return bridge->allowSelection || bridge->unsupported ? S_OK : S_FALSE;
  }

  HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog*, IShellItem*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog*, IShellItem*,
                                             FDE_SHAREVIOLATION_RESPONSE* response) override {
    *response = FDESVR_DEFAULT;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE* response) override {
    *response = FDEOR_DEFAULT;
    return S_OK;
  }

 private:
  CallbackBridge* bridge;
};

PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW_hook(LPBROWSEINFOW lpbi) {
  if (!lpbi) return SHBrowseForFolderW_orig(lpbi);

  if (lpbi->pidlRoot || (lpbi->ulFlags & ~(BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_NEWDIALOGSTYLE))) {
    return SHBrowseForFolderW_orig(lpbi);
  }

  winrt::com_ptr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()))) ||
      FAILED(dialog->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST))) {
    return SHBrowseForFolderW_orig(lpbi);
  }

  if (lpbi->lpszTitle && *lpbi->lpszTitle && FAILED(dialog->SetTitle(lpbi->lpszTitle))) {
    return SHBrowseForFolderW_orig(lpbi);
  }

  CallbackBridge bridge = {lpbi, dialog.get()};
  DWORD cookie = 0;
  if (lpbi->lpfn) {
    bridge.window =
      CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!bridge.window) return SHBrowseForFolderW_orig(lpbi);

    SetWindowLongPtrW(bridge.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&bridge));
    SetLastError(0);
    bridge.previousProc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(bridge.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CallbackWindowProc)));
    if (!bridge.previousProc && GetLastError() != 0) {
      DestroyWindow(bridge.window);
      return SHBrowseForFolderW_orig(lpbi);
    }

    lpbi->lpfn(bridge.window, BFFM_INITIALIZED, 0, lpbi->lParam);
    if (bridge.unsupported) {
      DestroyWindow(bridge.window);
      return SHBrowseForFolderW_orig(lpbi);
    }

    auto events = winrt::make_self<DialogEvents>(&bridge);
    if (FAILED(dialog->Advise(events.get(), &cookie))) {
      DestroyWindow(bridge.window);
      return SHBrowseForFolderW_orig(lpbi);
    }
  }

  HRESULT hr = dialog->Show(lpbi->hwndOwner);
  if (cookie) dialog->Unadvise(cookie);
  if (bridge.window) DestroyWindow(bridge.window);
  if (bridge.unsupported) return SHBrowseForFolderW_orig(lpbi);
  if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return nullptr;
  if (FAILED(hr)) return SHBrowseForFolderW_orig(lpbi);

  winrt::com_ptr<IShellItem> selected;
  if (FAILED(dialog->GetResult(selected.put()))) return SHBrowseForFolderW_orig(lpbi);

  PIDLIST_ABSOLUTE result = nullptr;
  if (FAILED(SHGetIDListFromObject(selected.get(), &result))) return SHBrowseForFolderW_orig(lpbi);

  if (lpbi->pszDisplayName) {
    PWSTR displayName = nullptr;
    if (FAILED(selected->GetDisplayName(SIGDN_NORMALDISPLAY, &displayName))) {
      CoTaskMemFree(result);
      return SHBrowseForFolderW_orig(lpbi);
    }
    lstrcpynW(lpbi->pszDisplayName, displayName, MAX_PATH);
    CoTaskMemFree(displayName);
  }

  SHFILEINFOW fileInfo = {};
  if (SHGetFileInfoW(reinterpret_cast<PCWSTR>(result), 0, &fileInfo, sizeof(fileInfo),
                     SHGFI_PIDL | SHGFI_SYSICONINDEX)) {
    lpbi->iImage = fileInfo.iIcon;
  }

  return result;
}

BOOL Wh_ModInit() {
  if (!WindhawkUtils::Wh_SetFunctionHookT(SHBrowseForFolderW, SHBrowseForFolderW_hook, &SHBrowseForFolderW_orig)) {
    Wh_Log(L"Failed to hook SHBrowseForFolderW");
    return FALSE;
  }

  return TRUE;
}
