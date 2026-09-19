
#define UNICODE
#define _UNICODE
#define NOMINMAX
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>
#include "core.hpp"
#include "cat.hpp"

namespace {
constexpr wchar_t ClassName[] = L"EdgeCatTimer.Settings.01";
constexpr wchar_t OverlayClass[] = L"EdgeCatTimer.Overlay.01";
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT_PTR Tick = 1;
enum Id { Hours=101, Minutes, Seconds, Preset25, Preset50, Preset60, Test10,
  Monitor, FullScreen, Size, Margin, LoadImage, DefaultImage, Mirror, Rotate,
  Sound, HideOnStart, Start, Pause, Stop, Hide, Exit, Asset, TimeLeft, Status };
struct Control { HWND window; int x,y,w,h; int font; };
struct MonitorInfo { RECT full, work; std::wstring device, name; bool primary; };
struct Frame { std::vector<uint32_t> pixels; uint64_t duration=125; };
struct Surface {
  HDC dc=nullptr; HBITMAP bitmap=nullptr; HGDIOBJ old=nullptr; uint32_t* pixels=nullptr;
  int size=0;
  void clear() {
    if (dc && old) SelectObject(dc,old);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    dc=nullptr; bitmap=nullptr; old=nullptr; pixels=nullptr; size=0;
  }
  bool create(int side) {
    clear(); size=side;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=side; info.bmiHeader.biHeight=-side;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    info.bmiHeader.biCompression=BI_RGB;
    dc=CreateCompatibleDC(nullptr);
    bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
    if (!dc || !bitmap || !pixels) { clear(); return false; }
    old=SelectObject(dc,bitmap);
    return true;
  }
  ~Surface() { clear(); }
};

uint64_t nowMs() {
  ULONGLONG ticks=0;
  // Excludes sleep/hibernation; changing the wall clock never changes the lap.
  QueryUnbiasedInterruptTime(&ticks);
  return ticks/10000;
}
std::wstring text(HWND window) {
  int length=GetWindowTextLengthW(window);
  std::wstring value(length+1,L'\0');
  GetWindowTextW(window,&value[0],length+1);
  value.resize(length); return value;
}
std::wstring formatTime(uint64_t milliseconds) {
  uint64_t seconds=(milliseconds+999)/1000;
  wchar_t result[64];
  swprintf(result,64,L"%02llu:%02llu:%02llu",static_cast<unsigned long long>(seconds/3600),
    static_cast<unsigned long long>((seconds/60)%60),static_cast<unsigned long long>(seconds%60));
  return result;
}

class Application;
Application* app=nullptr;
LRESULT CALLBACK MainProc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK OverlayProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  if (message==WM_NCHITTEST) return HTTRANSPARENT;
  if (message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  if (message==WM_ERASEBKGND) return 1;
  return DefWindowProcW(window,message,w,l);
}

class Application {
 public:
  HINSTANCE instance;
  HWND main=nullptr,overlay=nullptr;
  HICON icon=nullptr;
  HBRUSH background=CreateSolidBrush(RGB(247,248,245));
  HFONT fonts[4]{};
  std::vector<Control> controls;
  std::vector<MonitorInfo> monitors;
  std::vector<Frame> frames;
  Surface surface;
  edgecat::Countdown timer;
  NOTIFYICONDATAW tray{};
  std::wstring settingsPath,imagePath,monitorDevice;
  double scale=1.0;
  int side=72,margin=0,lastFrame=-1,lastRotation=-1,lastX=INT_MIN,lastY=INT_MIN;
  uint64_t animationLength=500,lastStatusSecond=UINT64_MAX;
  bool trayReady=false,quitting=false,clockHotkey=false,stopHotkey=false;
  UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
  std::vector<int> editable{Hours,Minutes,Seconds,Preset25,Preset50,Preset60,Test10,
    Monitor,FullScreen,Size,Margin,LoadImage,DefaultImage,Mirror,Rotate};

  explicit Application(HINSTANCE h):instance(h) {
    wchar_t path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,path))) {
      settingsPath=std::wstring(path)+L"\\EdgeCatTimer";
      CreateDirectoryW(settingsPath.c_str(),nullptr);
      settingsPath+=L"\\settings.ini";
    }
  }
  ~Application() {
    for(auto font:fonts) if(font) DeleteObject(font);
    DeleteObject(background);
  }
  HWND get(int id) { return GetDlgItem(main,id); }
  bool checked(int id) { return SendMessageW(get(id),BM_GETCHECK,0,0)==BST_CHECKED; }
  void check(int id,bool value) { SendMessageW(get(id),BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0); }
  int px(int v) const { return int(std::lround(v*scale)); }
  HWND add(const wchar_t* type,const wchar_t* label,int id,int x,int y,int w,int h,
           DWORD style=0,int font=0) {
    DWORD extra=wcscmp(type,L"EDIT")==0?WS_EX_CLIENTEDGE:0;
    HWND window=CreateWindowExW(extra,type,label,WS_CHILD|WS_VISIBLE|style,
      px(x),px(y),px(w),px(h),main,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    controls.push_back({window,x,y,w,h,font});
    return window;
  }
  void label(const wchar_t* value,int x,int y,int w,int h=24,int font=0) {
    add(L"STATIC",value,0,x,y,w,h,SS_LEFT,font);
  }
  void button(const wchar_t* value,int id,int x,int y,int w,int h=30) {
    add(L"BUTTON",value,id,x,y,w,h,WS_TABSTOP|BS_PUSHBUTTON);
  }
  void checkbox(const wchar_t* value,int id,int x,int y,int w,bool enabled) {
    add(L"BUTTON",value,id,x,y,w,26,WS_TABSTOP|BS_AUTOCHECKBOX); check(id,enabled);
  }
  void number(const wchar_t* value,int id,int x,int y,int w) {
    auto control=add(L"EDIT",value,id,x,y,w,30,WS_TABSTOP|ES_NUMBER|ES_CENTER|ES_AUTOHSCROLL);
    SendMessageW(control,EM_SETLIMITTEXT,id==Hours?3:2,0);
  }
  void setFonts() {
    HFONT previous[4]; std::copy(std::begin(fonts),std::end(fonts),previous);
    const int sizes[4]={15,26,36,12};
    for(int i=0;i<4;++i)
      fonts[i]=CreateFontW(-px(sizes[i]),0,0,0,i==1||i==2?FW_BOLD:FW_NORMAL,
        FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Malgun Gothic");
    for(auto& c:controls) SendMessageW(c.window,WM_SETFONT,reinterpret_cast<WPARAM>(fonts[c.font]),TRUE);
    for(auto font:previous) if(font) DeleteObject(font);
  }
  void layout(UINT dpi,RECT work) {
    scale=std::min(dpi/96.0,std::min((work.bottom-work.top-52)/628.0,(work.right-work.left-32)/600.0));
    scale=std::max(0.6,scale);
    RECT bounds{0,0,px(600),px(628)};
    AdjustWindowRectExForDpi(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
      FALSE,0,dpi);
    SetWindowPos(main,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,
      SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    for(auto& c:controls) SetWindowPos(c.window,nullptr,px(c.x),px(c.y),px(c.w),px(c.h),
      SWP_NOZORDER|SWP_NOACTIVATE);
    setFonts();
  }
  void createControls() {
    label(L"가장자리 타이머",24,18,552,38,1);
    label(L"한 바퀴",26,61,548,24);
    label(L"남은 시간",26,103,140,22,3);
    add(L"STATIC",L"00:25:00",TimeLeft,24,124,300,49,SS_LEFT,2);
    add(L"STATIC",L"준비",Status,348,139,228,30,SS_RIGHT);
    label(L"시간",24,187,48);
    number(L"0",Hours,76,180,55); label(L"시간",137,187,40);
    number(L"25",Minutes,183,180,55); label(L"분",244,187,28);
    number(L"0",Seconds,278,180,55); label(L"초",339,187,26);
    button(L"25분",Preset25,24,220,96); button(L"50분",Preset50,130,220,96);
    button(L"1시간",Preset60,236,220,96); button(L"10초 시험",Test10,342,220,234);
    label(L"모니터",24,273,70);
    add(L"COMBOBOX",L"",Monitor,101,266,475,160,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL);
    checkbox(L"작업표시줄을 포함한 화면 가장자리",FullScreen,24,305,552,true);
    label(L"캐릭터 크기",24,347,100);
    add(L"COMBOBOX",L"",Size,128,338,122,180,WS_TABSTOP|CBS_DROPDOWNLIST);
    for(int v:{48,64,72,96,128,160}) {
      std::wstring value=std::to_wstring(v)+L" px";
      LRESULT n=SendMessageW(get(Size),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value.c_str()));
      SendMessageW(get(Size),CB_SETITEMDATA,n,v);
    }
    SendMessageW(get(Size),CB_SETCURSEL,2,0);
    label(L"안쪽 여백",309,347,100);
    number(L"0",Margin,409,338,66); label(L"px",486,347,70);
    add(L"STATIC",L"캐릭터: 기본 이미지",Asset,24,382,552,24,SS_PATHELLIPSIS);
    button(L"이미지 / GIF 선택",LoadImage,24,411,205);
    button(L"기본 이미지",DefaultImage,241,411,151);
    checkbox(L"좌우 반전",Mirror,414,413,162,false);
    checkbox(L"발이 테두리에 닿도록 회전",Rotate,24,450,552,true);
    checkbox(L"완료 알림 소리",Sound,24,482,238,true);
    checkbox(L"시작하면 설정 창 숨기기",HideOnStart,282,482,294,true);
    button(L"시작",Start,24,524,176,38);
    button(L"일시정지",Pause,212,524,176,38);
    button(L"중단 / 초기화",Stop,400,524,176,38);
    label(L"Ctrl+Alt+P 일시정지·재개  /  Ctrl+Alt+S 중단",24,571,552,19,3);
    label(L"시계 옆 고양이 아이콘으로 설정을 다시 열 수 있어요.",24,594,394,21,3);
    button(L"종료",Exit,466,590,110,28);
    for(int id:{Hours,Minutes,Seconds,Margin}) SendMessageW(get(id),EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(3,3));
    refreshMonitors();
    loadSettings();
    rebuildFrames(false);
    setFonts();
    updateControls();
  }
  static BOOL CALLBACK enumMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM value) {
    auto self=reinterpret_cast<Application*>(value);
    MONITORINFOEXW mi{}; mi.cbSize=sizeof(mi);
    if(!GetMonitorInfoW(monitor,&mi)) return TRUE;
    DISPLAY_DEVICEW device{}; device.cb=sizeof(device);
    EnumDisplayDevicesW(mi.szDevice,0,&device,0);
    self->monitors.push_back({mi.rcMonitor,mi.rcWork,mi.szDevice,
      device.DeviceString[0]?device.DeviceString:mi.szDevice,(mi.dwFlags&MONITORINFOF_PRIMARY)!=0});
    return TRUE;
  }
  void refreshMonitors() {
    monitors.clear();
    EnumDisplayMonitors(nullptr,nullptr,enumMonitor,reinterpret_cast<LPARAM>(this));
    std::stable_sort(monitors.begin(),monitors.end(),[](auto& a,auto& b){return a.primary>b.primary;});
    SendMessageW(get(Monitor),CB_RESETCONTENT,0,0);
    int selected=0;
    for(size_t i=0;i<monitors.size();++i) {
      auto& m=monitors[i];
      std::wstring name=L"모니터 "+std::to_wstring(i+1)+(m.primary?L" (주 모니터)":L"")+
        L"  ·  "+std::to_wstring(m.full.right-m.full.left)+L" × "+std::to_wstring(m.full.bottom-m.full.top)+L"  ·  "+m.name;
      SendMessageW(get(Monitor),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
      if(m.device==monitorDevice) selected=int(i);
    }
    SendMessageW(get(Monitor),CB_SETCURSEL,selected,0);
    if(!monitors.empty()) monitorDevice=monitors[selected].device;
    invalidateSprite();
  }
  bool getNumber(int id,int minimum,int maximum,int& result) {
    auto value=text(get(id));
    if(value.empty()) return false;
    for(wchar_t c:value) if(c<L'0'||c>L'9') return false;
    long parsed=wcstol(value.c_str(),nullptr,10);
    if(parsed<minimum||parsed>maximum) return false;
    result=int(parsed); return true;
  }
  bool durationFromControls(uint64_t& value) {
    int h,m,s;
    if(!getNumber(Hours,0,999,h)||!getNumber(Minutes,0,59,m)||!getNumber(Seconds,0,59,s)) return false;
    value=(uint64_t(h)*3600+uint64_t(m)*60+uint64_t(s))*1000;
    return value>0;
  }
  void preset(int seconds) {
    if(timer.state==edgecat::State::Finished) {
      timer.reset();
      if(overlay) ShowWindow(overlay,SW_HIDE);
    }
    SetWindowTextW(get(Hours),std::to_wstring(seconds/3600).c_str());
    SetWindowTextW(get(Minutes),std::to_wstring(seconds/60%60).c_str());
    SetWindowTextW(get(Seconds),std::to_wstring(seconds%60).c_str());
    updateControls();
  }
  void loadSettings() {
    if(settingsPath.empty()) return;
    auto read=[&](const wchar_t* key,int fallback){return int(GetPrivateProfileIntW(L"Timer",key,fallback,settingsPath.c_str()));};
    preset(std::clamp(read(L"Seconds",1500),1,3599999));
    check(FullScreen,read(L"FullScreen",1)!=0); check(Rotate,read(L"Rotate",1)!=0);
    check(Mirror,read(L"Mirror",0)!=0); check(Sound,read(L"Sound",1)!=0);
    check(HideOnStart,read(L"HideOnStart",1)!=0);
    margin=std::clamp(read(L"Margin",0),0,99);
    SetWindowTextW(get(Margin),std::to_wstring(margin).c_str());
    side=std::clamp(read(L"Size",72),48,160);
    for(int i=0;i<6;++i) if(SendMessageW(get(Size),CB_GETITEMDATA,i,0)==side) SendMessageW(get(Size),CB_SETCURSEL,i,0);
    wchar_t value[32768]{};
    GetPrivateProfileStringW(L"Timer",L"Monitor",L"",value,32768,settingsPath.c_str()); monitorDevice=value;
    refreshMonitors();
    GetPrivateProfileStringW(L"Timer",L"Image",L"",value,32768,settingsPath.c_str()); imagePath=value;
    if(!imagePath.empty()&&GetFileAttributesW(imagePath.c_str())==INVALID_FILE_ATTRIBUTES) imagePath.clear();
  }
  void saveSettings() {
    if(settingsPath.empty()) return;
    // Initialize a UTF-16 INI so Korean image filenames round-trip correctly.
    if(GetFileAttributesW(settingsPath.c_str())==INVALID_FILE_ATTRIBUTES) {
      HANDLE file=CreateFileW(settingsPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(file!=INVALID_HANDLE_VALUE) { WORD bom=0xfeff; DWORD written; WriteFile(file,&bom,2,&written,nullptr); CloseHandle(file); }
    }
    auto put=[&](const wchar_t* key,const std::wstring& value){WritePrivateProfileStringW(L"Timer",key,value.c_str(),settingsPath.c_str());};
    uint64_t duration;
    if(durationFromControls(duration)) put(L"Seconds",std::to_wstring(duration/1000));
    put(L"Size",std::to_wstring(side)); put(L"Margin",std::to_wstring(margin));
    put(L"FullScreen",checked(FullScreen)?L"1":L"0"); put(L"Rotate",checked(Rotate)?L"1":L"0");
    put(L"Mirror",checked(Mirror)?L"1":L"0"); put(L"Sound",checked(Sound)?L"1":L"0");
    put(L"HideOnStart",checked(HideOnStart)?L"1":L"0");
    put(L"Monitor",monitorDevice); put(L"Image",imagePath);
  }
  void error(const wchar_t* message) { MessageBoxW(main,message,L"타이머",MB_OK|MB_ICONINFORMATION); }
  void invalidateSprite() { lastFrame=-1;lastRotation=-1;lastX=INT_MIN;lastY=INT_MIN; }
  bool customFrames(std::vector<Frame>& out,std::wstring& reason) {
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if(!GetFileAttributesExW(imagePath.c_str(),GetFileExInfoStandard,&attr)||attr.nFileSizeHigh||attr.nFileSizeLow>20*1024*1024) {
      reason=L"이미지를 읽을 수 없거나 파일이 20 MB를 넘습니다."; return false;
    }
    std::unique_ptr<Gdiplus::Bitmap> image(Gdiplus::Bitmap::FromFile(imagePath.c_str()));
    if(!image||image->GetLastStatus()!=Gdiplus::Ok) {reason=L"지원되는 PNG, JPG, GIF, BMP 파일을 선택해 주세요.";return false;}
    UINT w=image->GetWidth(),h=image->GetHeight();
    if(!w||!h||w>2048||h>2048) {reason=L"원본 이미지의 가로·세로는 각각 2048 px 이하여야 합니다.";return false;}
    UINT count=1;
    UINT dims=image->GetFrameDimensionsCount();
    std::vector<GUID> dimensions(dims);
    if(dims) image->GetFrameDimensionsList(dimensions.data(),dims);
    bool animated=false;
    for(auto& guid:dimensions) if(IsEqualGUID(guid,Gdiplus::FrameDimensionTime)) {
      count=image->GetFrameCount(&Gdiplus::FrameDimensionTime); animated=true; break;
    }
    if(count<1||count>200||uint64_t(w)*h*count>16*1024*1024||uint64_t(side)*side*count*4>32*1024*1024) {
      reason=L"이 GIF는 메모리 사용량이 너무 큽니다. 해상도나 프레임 수를 줄여 주세요.\n최대 200프레임, 원본 전체 1,677만 픽셀까지 지원합니다.";return false;
    }
    std::vector<uint32_t> delays(count,12);
    UINT propSize=image->GetPropertyItemSize(PropertyTagFrameDelay);
    if(propSize>=sizeof(Gdiplus::PropertyItem)) {
      std::vector<BYTE> data(propSize);
      auto property=reinterpret_cast<Gdiplus::PropertyItem*>(data.data());
      if(image->GetPropertyItem(PropertyTagFrameDelay,propSize,property)==Gdiplus::Ok&&property->value&&property->length>=count*4)
        std::copy_n(static_cast<uint32_t*>(property->value),count,delays.begin());
    }
    int dw=side,dh=side;
    if(w>h) dh=std::max(1,int(uint64_t(h)*side/w)); else dw=std::max(1,int(uint64_t(w)*side/h));
    int minX=side,minY=side,maxX=-1,maxY=-1;
    for(UINT index=0;index<count;++index) {
      if(animated&&image->SelectActiveFrame(&Gdiplus::FrameDimensionTime,index)!=Gdiplus::Ok) {
        reason=L"GIF의 프레임을 읽지 못했습니다.";return false;
      }
      Gdiplus::Bitmap scaleBitmap(side,side,PixelFormat32bppPARGB);
      {
        Gdiplus::Graphics graphics(&scaleBitmap);
        graphics.Clear(Gdiplus::Color(0,0,0,0));
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        if(graphics.DrawImage(image.get(),Gdiplus::Rect((side-dw)/2,side-dh,dw,dh),0,0,w,h,Gdiplus::UnitPixel)!=Gdiplus::Ok) {
          reason=L"이미지를 표시할 수 없습니다.";return false;
        }
      }
      Frame frame; frame.duration=count==1?1000:std::clamp<uint64_t>(uint64_t(delays[index])*10,50,60000);
      frame.pixels.resize(side*side);
      Gdiplus::BitmapData bits{}; Gdiplus::Rect rectangle(0,0,side,side);
      if(scaleBitmap.LockBits(&rectangle,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&bits)!=Gdiplus::Ok) {
        reason=L"이미지 메모리를 읽을 수 없습니다.";return false;
      }
      for(int y=0;y<side;++y) {
        auto row=reinterpret_cast<uint32_t*>(static_cast<BYTE*>(bits.Scan0)+y*bits.Stride);
        for(int x=0;x<side;++x) {
          uint32_t pixel=row[x]; frame.pixels[y*side+x]=pixel;
          if(pixel>>24) {minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);}
        }
      }
      scaleBitmap.UnlockBits(&bits); out.push_back(std::move(frame));
    }
    if(maxX<0) {reason=L"이미지가 완전히 투명합니다.";return false;}
    // A shared alpha crop avoids per-frame jumping and puts the feet at the edge.
    int cw=maxX-minX+1,ch=maxY-minY+1;
    int width=side,height=side;
    if(cw>ch) height=std::max(1,ch*side/cw); else width=std::max(1,cw*side/ch);
    for(auto& frame:out) {
      std::vector<uint32_t> crop(side*side,0);
      for(int y=0;y<height;++y) for(int x=0;x<width;++x)
        crop[(side-height+y)*side+(side-width)/2+x]=frame.pixels[(minY+y*ch/height)*side+minX+x*cw/width];
      frame.pixels.swap(crop);
    }
    return true;
  }
  bool rebuildFrames(bool report) {
    LRESULT selected=SendMessageW(get(Size),CB_GETCURSEL,0,0);
    int requested=int(SendMessageW(get(Size),CB_GETITEMDATA,selected,0));
    side=std::clamp(requested,48,160);
    std::vector<Frame> fresh;
    if(!imagePath.empty()) {
      std::wstring reason;
      if(!customFrames(fresh,reason)) {
        if(report) error(reason.c_str());
        imagePath.clear(); fresh.clear();
      }
    }
    if(imagePath.empty()) {
      for(int f=0;f<4;++f) {
        auto source=catFrame(f); Frame frame; frame.pixels.resize(side*side);
        for(int y=0;y<side;++y) for(int x=0;x<side;++x)
          frame.pixels[y*side+x]=source[(y*32/side)*32+x*32/side];
        fresh.push_back(std::move(frame));
      }
    }
    if(!surface.create(side)) {error(L"캐릭터 표시 메모리를 만들지 못했습니다.");return false;}
    frames.swap(fresh); animationLength=0;
    for(auto& frame:frames) animationLength+=frame.duration;
    std::wstring caption=L"캐릭터: "+(imagePath.empty()?std::wstring(L"기본 이미지"):imagePath);
    SetWindowTextW(get(Asset),caption.c_str());
    invalidateSprite();return true;
  }
  void chooseImage() {
    wchar_t path[32768]{};
    OPENFILENAMEW file{}; file.lStructSize=sizeof(file);file.hwndOwner=main;
    file.lpstrFile=path;file.nMaxFile=32768;file.lpstrTitle=L"오른쪽을 보는 캐릭터 이미지 선택";
    file.lpstrFilter=L"이미지 (PNG, GIF, JPG, BMP)\0*.png;*.gif;*.jpg;*.jpeg;*.bmp\0\0";
    file.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    if(GetOpenFileNameW(&file)) {
      imagePath=path; rebuildFrames(true);
      if(timer.state==edgecat::State::Finished) render();
      saveSettings();
    }
  }
  edgecat::Rect route() {
    if(monitors.empty()) return {0,0,800,600};
    LRESULT index=SendMessageW(get(Monitor),CB_GETCURSEL,0,0);
    if(index<0||index>=int(monitors.size())) index=0;
    auto& m=monitors[index]; monitorDevice=m.device;
    RECT bounds=checked(FullScreen)?m.full:m.work;
    int maximum=std::max(0,(int(std::min(bounds.right-bounds.left,bounds.bottom-bounds.top))-side)/2);
    int inset=std::min(margin,maximum);
    return {bounds.left+inset,bounds.top+inset,bounds.right-bounds.left-2*inset,bounds.bottom-bounds.top-2*inset};
  }
  void render() {
    if(!surface.pixels||frames.empty()||!overlay) return;
    uint64_t now=nowMs(),phase=timer.elapsed(now)%animationLength;
    int frame=0;
    while(frame+1<int(frames.size())&&phase>=frames[frame].duration) {phase-=frames[frame].duration;++frame;}
    if(timer.state==edgecat::State::Finished) frame=0;
    auto pose=edgecat::position(route(),side,timer.progress(now));
    int rotation=checked(Rotate)?pose.quarterTurns:0;
    if(!checked(Rotate)&&(pose.quarterTurns==2||pose.quarterTurns==3)) rotation=4;
    bool mirror=checked(Mirror);
    int cacheRotation=rotation+(mirror?8:0);
    bool changed=frame!=lastFrame||cacheRotation!=lastRotation;
    if(changed) {
      auto& src=frames[frame].pixels;
      for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
        int sx=mirror?side-1-x:x, dx=x,dy=y;
        if(rotation==1) {dx=side-1-y;dy=x;}
        else if(rotation==2) {dx=side-1-x;dy=side-1-y;}
        else if(rotation==3) {dx=y;dy=side-1-x;}
        else if(rotation==4) {dx=side-1-x;dy=y;}
        surface.pixels[dy*side+dx]=src[y*side+sx];
      }
      lastFrame=frame;lastRotation=cacheRotation;
    }
    if(changed||pose.x!=lastX||pose.y!=lastY) {
      POINT destination{pose.x,pose.y},source{0,0}; SIZE dimensions{side,side};
      BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
      if(!UpdateLayeredWindow(overlay,nullptr,&destination,&dimensions,surface.dc,&source,0,&blend,ULW_ALPHA)) {
        timer.pause(now); KillTimer(main,Tick); ShowWindow(overlay,SW_HIDE);
        showSettings(); updateControls();
        error(L"투명 창을 표시하지 못해 일시정지했습니다. 화면 설정을 확인한 뒤 다시 시작해 주세요.");
        return;
      }
      lastX=pose.x;lastY=pose.y;
    }
    ShowWindow(overlay,SW_SHOWNOACTIVATE);
  }
  void updateTrayTip() {
    if(!trayReady) return;
    std::wstring value=L"타이머 · ";
    if(timer.state==edgecat::State::Ready) value+=L"준비";
    else if(timer.state==edgecat::State::Finished) value+=L"완료";
    else value+=formatTime(timer.remaining(nowMs()))+(timer.state==edgecat::State::Paused?L" · 일시정지":L"");
    wcsncpy_s(tray.szTip,value.c_str(),_TRUNCATE);
    tray.uFlags=NIF_TIP;Shell_NotifyIconW(NIM_MODIFY,&tray);
  }
  void updateControls() {
    bool active=timer.state==edgecat::State::Running||timer.state==edgecat::State::Paused;
    for(int id:editable) EnableWindow(get(id),!active);
    EnableWindow(get(Pause),active);
    EnableWindow(get(Stop),timer.state!=edgecat::State::Ready);
    SetWindowTextW(get(Start),active?L"처음부터 시작":L"시작");
    SetWindowTextW(get(Pause),timer.state==edgecat::State::Paused?L"재개":L"일시정지");
    const wchar_t* state=L"준비";
    if(timer.state==edgecat::State::Running) state=L"걷는 중";
    if(timer.state==edgecat::State::Paused) state=L"일시정지";
    if(timer.state==edgecat::State::Finished) state=L"한 바퀴 완료!";
    SetWindowTextW(get(Status),state);
    uint64_t remaining=timer.remaining(nowMs());
    if(timer.state==edgecat::State::Ready) { uint64_t input; if(durationFromControls(input)) remaining=input; }
    SetWindowTextW(get(TimeLeft),formatTime(remaining).c_str());
    updateTrayTip();
  }
  void start() {
    uint64_t duration;
    if(!durationFromControls(duration)) {error(L"1초 이상으로 설정해 주세요.\n시간: 0~999 / 분·초: 0~59");return;}
    if(!getNumber(Margin,0,99,margin)) {error(L"여백을 0~99 px로 입력해 주세요.");return;}
    if(monitors.empty()) {error(L"사용할 모니터가 없습니다.");return;}
    auto area=route();
    if(area.width<side||area.height<side) {error(L"선택한 화면이 캐릭터보다 작습니다. 크기를 줄여 주세요.");return;}
    if(!overlay) {error(L"캐릭터 창을 만들지 못했습니다. 프로그램을 다시 실행해 주세요.");return;}
    if(frames.empty()&&!rebuildFrames(true)) return;
    timer.start(duration,nowMs()); invalidateSprite(); lastStatusSecond=UINT64_MAX;
    if(!SetTimer(main,Tick,50,nullptr)) {timer.reset();updateControls();error(L"타이머를 시작하지 못했습니다.");return;}
    render(); updateControls(); saveSettings();
    if(checked(HideOnStart)&&trayReady&&timer.state==edgecat::State::Running) ShowWindow(main,SW_HIDE);
  }
  void finish() {
    KillTimer(main,Tick); render(); updateControls();
    if(checked(Sound)) MessageBeep(MB_ICONASTERISK);
    if(trayReady) {
      tray.uFlags=NIF_INFO; tray.dwInfoFlags=NIIF_INFO|NIIF_NOSOUND;
      wcscpy_s(tray.szInfoTitle,L"한 바퀴 완료!");
      wcscpy_s(tray.szInfo,L"설정한 시간이 끝났습니다.");
      Shell_NotifyIconW(NIM_MODIFY,&tray);
    }
  }
  void pause() {
    if(timer.state==edgecat::State::Running) {
      uint64_t now=nowMs();
      if(timer.update(now)) {finish();return;}
      timer.pause(now); KillTimer(main,Tick); render();
    } else if(timer.state==edgecat::State::Paused) {
      if(!SetTimer(main,Tick,50,nullptr)) {error(L"타이머를 재개하지 못했습니다.");return;}
      timer.resume(nowMs());
    }
    updateControls();
  }
  void stop() {
    timer.reset();KillTimer(main,Tick);ShowWindow(overlay,SW_HIDE);invalidateSprite();updateControls();
  }
  void tick() {
    uint64_t now=nowMs();
    if(timer.update(now)) {finish();return;}
    render();
    uint64_t sec=(timer.remaining(now)+999)/1000;
    if(sec!=lastStatusSecond) {lastStatusSecond=sec;updateControls();}
  }
  void addTray() {
    tray={};tray.cbSize=sizeof(tray);tray.hWnd=main;tray.uID=1;
    tray.uCallbackMessage=TrayMessage;tray.hIcon=icon;
    tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
    wcscpy_s(tray.szTip,L"타이머");
    trayReady=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;
    updateTrayTip();
    if(!trayReady) showSettings();
  }
  void showSettings() {ShowWindow(main,SW_RESTORE);SetForegroundWindow(main);updateControls();}
  void trayMenu() {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,Hide,L"설정 열기");
    AppendMenuW(menu,MF_STRING,Start,L"처음부터 시작");
    AppendMenuW(menu,MF_STRING|((timer.state==edgecat::State::Running||timer.state==edgecat::State::Paused)?0:MF_GRAYED),
      Pause,timer.state==edgecat::State::Paused?L"재개":L"일시정지");
    AppendMenuW(menu,MF_STRING,Stop,L"중단 / 초기화");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,Exit,L"종료");
    POINT p;GetCursorPos(&p);SetForegroundWindow(main);
    int result=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,main,nullptr);
    DestroyMenu(menu);PostMessageW(main,WM_NULL,0,0);
    if(result) command(result,BN_CLICKED);
  }
  void command(int id,int notification) {
    if(id==Start) start();
    else if(id==Pause) pause();
    else if(id==Stop) stop();
    else if(id==Exit) {quitting=true;DestroyWindow(main);}
    else if(id==Hide) showSettings();
    else if(id==Preset25) preset(1500);
    else if(id==Preset50) preset(3000);
    else if(id==Preset60) preset(3600);
    else if(id==Test10) preset(10);
    else if(id==LoadImage) chooseImage();
    else if(id==DefaultImage) {imagePath.clear();check(Mirror,false);rebuildFrames(true);saveSettings();}
    else if(id==Size&&notification==CBN_SELCHANGE) {rebuildFrames(true);saveSettings();}
    else if(id==Monitor&&notification==CBN_SELCHANGE) {route();saveSettings();invalidateSprite();}
    else if(id==FullScreen||id==Mirror||id==Rotate||id==Sound||id==HideOnStart) {invalidateSprite();saveSettings();}
    else if((id==Hours||id==Minutes||id==Seconds)&&notification==EN_CHANGE) {
      if(timer.state==edgecat::State::Ready) {
        uint64_t duration;if(durationFromControls(duration)) SetWindowTextW(get(TimeLeft),formatTime(duration).c_str());
      }
    }
    if(timer.state==edgecat::State::Finished&&id!=Exit) render();
  }
  void destroy() {
    saveSettings();KillTimer(main,Tick);
    if(clockHotkey) UnregisterHotKey(main,1);
    if(stopHotkey) UnregisterHotKey(main,2);
    if(trayReady) {tray.uFlags=0;Shell_NotifyIconW(NIM_DELETE,&tray);trayReady=false;}
    if(overlay) {DestroyWindow(overlay);overlay=nullptr;}
    PostQuitMessage(0);
  }
};

LRESULT CALLBACK MainProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  if(!app) return DefWindowProcW(window,message,w,l);
  if(message==app->taskbarCreated) {app->addTray();return 0;}
  switch(message) {
    case WM_CREATE: app->main=window;return 0;
    case WM_COMMAND: app->command(LOWORD(w),HIWORD(w));return 0;
    case WM_TIMER: if(w==Tick) app->tick();return 0;
    case WM_HOTKEY: if(w==1) app->pause();else if(w==2) app->stop();return 0;
    case TrayMessage:
      if(l==WM_LBUTTONUP||l==WM_LBUTTONDBLCLK||l==NIN_BALLOONUSERCLICK) app->showSettings();
      else if(l==WM_RBUTTONUP||l==WM_CONTEXTMENU) app->trayMenu();
      return 0;
    case WM_CLOSE:
      if(app->trayReady) ShowWindow(window,SW_HIDE);else DestroyWindow(window);
      return 0;
    case WM_SIZE:
      if(w==SIZE_MINIMIZED&&app->trayReady) ShowWindow(window,SW_HIDE);
      break;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
      if(app->get(Monitor)) {
        app->refreshMonitors();
        if(app->timer.state!=edgecat::State::Ready) app->render();
      }
      return 0;
    case WM_DPICHANGED: {
      auto bounds=reinterpret_cast<RECT*>(l);
      SetWindowPos(window,nullptr,bounds->left,bounds->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
      MONITORINFO info{};info.cbSize=sizeof(info);GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info);
      app->layout(HIWORD(w),info.rcWork);return 0;
    }
    case WM_CTLCOLORSTATIC:
      SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);
      SetTextColor(reinterpret_cast<HDC>(w),RGB(35,65,59));
      return reinterpret_cast<LRESULT>(app->background);
    case WM_ERASEBKGND: {
      RECT rect;GetClientRect(window,&rect);FillRect(reinterpret_cast<HDC>(w),&rect,app->background);return 1;
    }
    case WM_DESTROY: app->destroy();return 0;
  }
  return DefWindowProcW(window,message,w,l);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
  HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\EdgeCatTimer.01");
  if(mutex&&GetLastError()==ERROR_ALREADY_EXISTS) {
    HWND existing=FindWindowW(ClassName,nullptr);
    if(existing) {ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}
    CloseHandle(mutex);return 0;
  }
  ULONG_PTR gdiplus=0;Gdiplus::GdiplusStartupInput startup;
  if(Gdiplus::GdiplusStartup(&gdiplus,&startup,nullptr)!=Gdiplus::Ok) {
    MessageBoxW(nullptr,L"Windows 이미지 기능을 시작하지 못했습니다.",L"타이머",MB_ICONERROR);
    if(mutex) CloseHandle(mutex);return 1;
  }
  INITCOMMONCONTROLSEX init{sizeof(init),ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
  int result=0;
  {
    Application application(instance);app=&application;
    application.icon=LoadIconW(instance,MAKEINTRESOURCEW(101));
    if(!application.icon) application.icon=LoadIconW(nullptr,IDI_APPLICATION);
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=MainProc;
    cls.lpszClassName=ClassName;cls.hIcon=application.icon;cls.hIconSm=application.icon;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=application.background;
    RegisterClassExW(&cls);
    cls.lpszClassName=OverlayClass;cls.lpfnWndProc=OverlayProc;cls.hbrBackground=nullptr;
    RegisterClassExW(&cls);
    HWND main=CreateWindowExW(0,ClassName,L"가장자리 타이머 0.1",
      WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,616,680,
      nullptr,nullptr,instance,nullptr);
    if(!main) {MessageBoxW(nullptr,L"설정 창을 만들지 못했습니다.",L"타이머",MB_ICONERROR);result=1;}
    else {
      // No owner: hiding or minimizing Settings must not hide the overlay.
      application.overlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
        OverlayClass,L"",WS_POPUP,0,0,72,72,nullptr,nullptr,instance,nullptr);
      application.createControls();
      MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(main,MONITOR_DEFAULTTONEAREST),&monitor);
      application.layout(GetDpiForWindow(main),monitor.rcWork);
      application.addTray();
      application.clockHotkey=RegisterHotKey(main,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'P')!=FALSE;
      application.stopHotkey=RegisterHotKey(main,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'S')!=FALSE;
      ShowWindow(main,show);UpdateWindow(main);
      if(!application.clockHotkey||!application.stopHotkey)
        application.error(L"다른 프로그램이 단축키를 사용 중입니다.\n일시정지와 중단은 설정 창 또는 시계 옆 아이콘 메뉴에서 사용할 수 있어요.");
      MSG message;
      while(GetMessageW(&message,nullptr,0,0)>0) {
        if(!IsDialogMessageW(main,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
      }
      result=int(message.wParam);
    }
    app=nullptr;
  }
  Gdiplus::GdiplusShutdown(gdiplus);
  if(mutex) CloseHandle(mutex);
  return result;
}
