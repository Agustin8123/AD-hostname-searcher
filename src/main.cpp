#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <atomic>
#include <memory>
#include <thread>
#include "windows_services.h"

namespace {
enum { Prefix=101, Fixed, Digits, Range, From, To, Families, All, None, Apply, Refresh, Ping, Cancel, Filter, State, Table, Empty, Details };
HWND window{};
std::map<int,HWND> controls;
HFONT font{}, titleFont{}, numberFont{}, smallFont{};
HBRUSH background{}, white{};
int dpi=96, width=1240, height=860;
ad::Config config;
std::optional<std::pair<int,int>> appliedRange;
std::vector<ad::Computer> computers;
ad::Inventory inventory;
std::vector<ad::Row> rows;
std::vector<size_t> visible;
std::vector<std::wstring> families;
std::wstring status=L"Configure la nomenclatura y pulse Consultar AD para comenzar.";
std::wstring settingsPath;
bool loadingFamilies=false, loaded=false, closing=false;
int registered=0, available=0, conflicts=0;
struct Job {
    std::atomic_bool cancel{false}, done{false};
    std::atomic_int progress{0};
    bool directory=false;
    int total=0;
    std::string error;
    std::vector<ad::Computer> computers;
    std::vector<ad::Row> rows;
    std::thread thread;
    ~Job() { if (thread.joinable()) thread.join(); }
};
std::unique_ptr<Job> job;
int px(int n) { return MulDiv(n,dpi,96); }
std::wstring text(HWND h) { int n=GetWindowTextLengthW(h); std::wstring s(n+1,L'\0'); GetWindowTextW(h,s.data(),n+1); s.resize(n); return s; }
std::wstring widen(const std::string& s) { return std::wstring(s.begin(),s.end()); }
void message(const std::string& s) { MessageBoxW(window,widen(s).c_str(),L"AD Hostname Searcher",MB_OK|MB_ICONWARNING); }
void invalidate() { InvalidateRect(window,nullptr,FALSE); }
HWND add(int id,const wchar_t* cls,const wchar_t* label,DWORD style=0) {
    HWND h=CreateWindowExW(cls==std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0,cls,label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    controls[id]=h; SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); SetWindowTheme(h,L"Explorer",nullptr); return h;
}
void place(int id,int x,int y,int w,int h) { MoveWindow(controls[id],px(x),px(y),px(w),px(h),TRUE); }
void layout() {
    place(Refresh,width-370,30,160,38); place(Ping,width-198,30,174,38);
    place(Prefix,40,252,256,28); place(Fixed,40,314,152,28); place(Digits,208,314,88,28);
    place(Range,40,376,256,140); place(From,40,416,118,28); place(To,178,416,118,28);
    place(Apply,40,458,256,32);
    place(Families,40,538,256,std::max(80,height-690));
    place(All,40,height-140,122,30); place(None,174,height-140,122,30);
    place(Filter,344,226,std::max(160,width-648),30); place(State,width-288,226,264,180);
    place(Table,344,274,width-368,height-380);
    place(Empty,374,375,width-428,70);
    place(Cancel,width-164,height-59,140,30);
    place(Details,width-164,height-59,140,30);
    invalidate();
}
int integer(int id) {
    auto s=text(controls[id]);
    if (s.empty() || s.size()>6 || s.find_first_not_of(L"0123456789")!=std::wstring::npos) throw std::runtime_error("Ingrese numeros enteros entre 0 y 999999.");
    return std::stoi(s);
}
ad::Config readConfig() { ad::Config c; c.prefix=ad::upper(text(controls[Prefix])); c.fixed=ad::upper(text(controls[Fixed])); c.digits=integer(Digits); c.validate(); return c; }
std::optional<std::pair<int,int>> readRange(const ad::Config& c) {
    int mode=static_cast<int>(SendMessageW(controls[Range],CB_GETCURSEL,0,0));
    if(mode==0) return {};
    if(mode==1) return std::make_pair(0,c.maximum());
    int first=integer(From),last=integer(To);
    if(first>last || last>c.maximum()) throw std::runtime_error("El rango no es valido para la cantidad de digitos configurada.");
    return std::make_pair(first,last);
}
void saveConfig() {
    WritePrivateProfileStringW(L"Naming",L"Prefix",config.prefix.c_str(),settingsPath.c_str());
    WritePrivateProfileStringW(L"Naming",L"Fixed",config.fixed.c_str(),settingsPath.c_str());
    WritePrivateProfileStringW(L"Naming",L"Digits",std::to_wstring(config.digits).c_str(),settingsPath.c_str());
}
std::set<std::wstring> selectedFamilies() {
    std::set<std::wstring> result;
    for(size_t i=0;i<families.size();++i) if(ListView_GetCheckState(controls[Families],static_cast<int>(i))) result.insert(families[i]);
    return result;
}
void filterRows() {
    visible.clear(); registered=available=conflicts=0;
    auto selected=selectedFamilies(); auto search=ad::upper(text(controls[Filter]));
    int state=static_cast<int>(SendMessageW(controls[State],CB_GETCURSEL,0,0));
    for(size_t i=0;i<rows.size();++i) {
        const auto& r=rows[i];
        if(!selected.count(r.family)) continue;
        if(!search.empty() && ad::upper(r.name+L" "+r.ip).find(search)==std::wstring::npos) continue;
        bool conflict=!r.inAD && r.network==ad::Network::Responding;
        if((state==1 && r.inAD) || (state==2 && !r.inAD) || (state==3 && !conflict)) continue;
        visible.push_back(i); registered+=r.inAD; available+=!r.inAD; conflicts+=conflict;
    }
    ListView_SetItemCountEx(controls[Table],static_cast<int>(visible.size()),0); InvalidateRect(controls[Table],nullptr,TRUE); invalidate();
    SetWindowTextW(controls[Empty],loaded ? L"No hay resultados para los filtros actuales.\nRevise las familias, el formato o el rango." : L"Su inventario aparecerá aquí.\nConfigure el formato y consulte Active Directory.");
    ShowWindow(controls[Empty],visible.empty() ? SW_SHOW : SW_HIDE);
}
void rebuild() {
    auto c=readConfig(); auto range=readRange(c);
    auto nextInventory=ad::classify(computers,c); auto nextRows=ad::report(nextInventory,c,range);
    auto previous=selectedFamilies(); bool first=families.empty();
    config=c; appliedRange=range; inventory=std::move(nextInventory); rows=std::move(nextRows); saveConfig();
    loadingFamilies=true; ListView_DeleteAllItems(controls[Families]); families.clear();
    for(const auto& [family,pcs]:inventory.families) {
        (void)pcs; int index=static_cast<int>(families.size()); families.push_back(family);
        LVITEMW item{}; item.mask=LVIF_TEXT; item.iItem=index; item.pszText=const_cast<LPWSTR>(family.c_str());
        ListView_InsertItem(controls[Families],&item); ListView_SetCheckState(controls[Families],index,first || previous.count(family));
    }
    loadingFamilies=false; filterRows();
    status=std::to_wstring(computers.size())+L" equipos en AD · "+std::to_wstring(inventory.families.size())+L" familias · "+std::to_wstring(inventory.unmatched.size())+L" fuera de nomenclatura";
    if(families.empty()) status+=L". Revise el prefijo, bloque fijo y dígitos.";
    invalidate();
}
void busy(bool active) {
    for(auto [id,h]:controls) if(id!=Cancel && id!=Table) EnableWindow(h,!active);
    EnableWindow(controls[Ping],!active && loaded && !rows.empty());
    EnableWindow(controls[Apply],!active && loaded);
    EnableWindow(controls[From],!active && SendMessageW(controls[Range],CB_GETCURSEL,0,0)==2);
    EnableWindow(controls[To],!active && SendMessageW(controls[Range],CB_GETCURSEL,0,0)==2);
    ShowWindow(controls[Cancel],active ? SW_SHOW : SW_HIDE);
    ShowWindow(controls[Details],active ? SW_HIDE : SW_SHOW);
    EnableWindow(controls[Details],loaded && !active);
}
LRESULT CALLBACK detailsProcedure(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_SIZE) { MoveWindow(GetDlgItem(h,1),0,0,LOWORD(lp),HIWORD(lp),TRUE); return 0; }
    return DefWindowProcW(h,msg,wp,lp);
}
void showDetails() {
    std::wstring detail=L"PRIMER CANDIDATO POR FAMILIA SELECCIONADA\r\nNo registrado en AD y sin respuesta positiva de ping; requiere revision.\r\n\r\n";
    auto selected=selectedFamilies();
    for(const auto& family:selected) {
        auto first=std::find_if(rows.begin(),rows.end(),[&](const auto& row) {
            return row.family==family && !row.inAD && row.network!=ad::Network::Responding;
        });
        detail+=family+L": "+(first==rows.end() ? L"Ninguno en el rango" : first->name+L" ("+ad::networkLabel(*first)+L")")+L"\r\n";
    }
    detail+=L"\r\nNOMBRES FUERA DE NOMENCLATURA\r\n";
    for(const auto& pc:inventory.unmatched) detail+=pc.name+L"\r\n";
    if(inventory.unmatched.empty()) detail+=L"Ninguno\r\n";
    WNDCLASSW cls{}; cls.lpfnWndProc=detailsProcedure; cls.hInstance=GetModuleHandleW(nullptr); cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.lpszClassName=L"ADHostnameDetails";
    RegisterClassW(&cls);
    HWND h=CreateWindowExW(0,cls.lpszClassName,L"Resumen del inventario",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,px(760),px(540),window,nullptr,cls.hInstance,nullptr);
    RECT r{}; GetClientRect(h,&r);
    HWND edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",detail.c_str(),WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL,0,0,r.right,r.bottom,h,reinterpret_cast<HMENU>(1),cls.hInstance,nullptr);
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); ShowWindow(h,SW_SHOW);
}
void startDirectory() {
    readConfig(); readRange(readConfig());
    job=std::make_unique<Job>(); job->directory=true; busy(true);
    status=L"Consultando Active Directory con su sesión de Windows…"; invalidate();
    Job* work=job.get();
    work->thread=std::thread([work] {
        try { work->computers=ad::queryDirectory(work->cancel); }
        catch(const std::exception& e) { work->error=e.what(); }
        work->done=true;
    });
}
void startPing() {
    auto selected=selectedFamilies();
    if(selected.empty()) throw std::runtime_error("Seleccione al menos una familia.");
    // Require applying edited naming/range before pinging to avoid stale hostnames.
    auto c=readConfig(); auto range=readRange(c);
    if(c.prefix!=config.prefix || c.fixed!=config.fixed || c.digits!=config.digits || range!=appliedRange)
        throw std::runtime_error("Pulse Aplicar formato y rango antes de verificar la red.");
    job=std::make_unique<Job>(); job->rows=rows;
    std::vector<size_t> indexes;
    for(size_t i=0;i<rows.size();++i) if(selected.count(rows[i].family)) indexes.push_back(i);
    job->total=static_cast<int>(indexes.size()); busy(true); status=L"Verificando DNS e ICMP de las familias seleccionadas…"; invalidate();
    Job* work=job.get();
    work->thread=std::thread([work,indexes=std::move(indexes)] {
        try {
            std::atomic_size_t next{0};
            std::vector<std::thread> pool;
            auto run=[&] {
                while(!work->cancel) {
                    size_t n=next.fetch_add(1); if(n>=indexes.size()) break;
                    auto& row=work->rows[indexes[n]]; row.ip.clear(); row.network=ad::Network::Unchecked;
                    try { ad::ping(row); } catch(...) { row.network=ad::Network::Error; }
                    ++work->progress;
                }
            };
            try { for(size_t n=0;n<std::min<size_t>(30,indexes.size());++n) pool.emplace_back(run); }
            catch(...) { work->cancel=true; for(auto& thread:pool) thread.join(); throw; }
            for(auto& thread:pool) thread.join();
        } catch(const std::exception& e) { work->error=e.what(); }
        work->done=true;
    });
}
std::wstring cell(const ad::Row& row,int column) {
    switch(column) {
    case 0:return row.name;
    case 1:return row.ip.empty() ? (row.network==ad::Network::Unchecked ? L"—" : L"No disponible") : row.ip;
    case 2:return row.family;
    case 3:return row.inAD ? L"Registrado" : L"No registrado";
    default:return ad::networkLabel(row);
    }
}
void drawText(HDC dc,const std::wstring& s,int x,int y,int w,int h,HFONT f,COLORREF color) {
    SelectObject(dc,f); SetTextColor(dc,color); SetBkMode(dc,TRANSPARENT);
    RECT r{px(x),px(y),px(x+w),px(y+h)}; DrawTextW(dc,s.c_str(),-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
void card(HDC dc,int x,int w,const wchar_t* label,int count,COLORREF color) {
    RECT r{px(x),px(106),px(x+w),px(194)}; FillRect(dc,&r,white);
    drawText(dc,label,x+20,118,w-40,22,smallFont,RGB(94,108,128));
    drawText(dc,std::to_wstring(count),x+20,145,w-40,36,numberFont,color);
}
void paint() {
    PAINTSTRUCT ps; HDC dc=BeginPaint(window,&ps); FillRect(dc,&ps.rcPaint,background);
    RECT header{0,0,px(width),px(88)}; FillRect(dc,&header,white);
    drawText(dc,L"AD / Hostname Searcher",24,17,width-430,32,titleFont,RGB(23,40,65));
    drawText(dc,L"Inventario y disponibilidad de nombres · Active Directory",25,51,width-430,23,smallFont,RGB(94,108,128));
    int cw=(width-108)/4;
    card(dc,24,cw,L"RESULTADOS VISIBLES",static_cast<int>(visible.size()),RGB(27,65,116));
    card(dc,44+cw,cw,L"REGISTRADOS EN AD",registered,RGB(27,65,116));
    card(dc,64+cw*2,cw,L"NO REGISTRADOS",available,RGB(20,116,98));
    card(dc,84+cw*3,cw,L"CONFLICTOS DE RED",conflicts,RGB(180,72,42));
    RECT sidebar{px(24),px(212),px(312),px(height-96)}; FillRect(dc,&sidebar,white);
    auto label=[&](const wchar_t* s,int x,int y,int w=256) { drawText(dc,s,x,y,w,24,smallFont,RGB(78,93,114)); };
    label(L"PREFIJO DEL NOMBRE",40,224); label(L"BLOQUE FIJO",40,286,150); label(L"DÍGITOS",208,286,88);
    label(L"RANGO DE NÚMEROS",40,348); label(L"FAMILIAS · SELECCIÓN MÚLTIPLE",40,506);
    drawText(dc,L"Buscar nombre o IP",344,202,300,20,smallFont,RGB(78,93,114));
    drawText(dc,L"Estado en el directorio",width-288,202,264,20,smallFont,RGB(78,93,114));
    drawText(dc,L"Un nombre sin registro en AD requiere revisión antes de asignarlo. Un ping sin respuesta no confirma que esté libre.",344,height-98,width-368,30,smallFont,RGB(94,108,128));
    drawText(dc,status,24,height-60,width-210,32,smallFont,RGB(52,72,95));
    EndPaint(window,&ps);
}
void init() {
    HDC dc=GetDC(window); dpi=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(window,dc);
    auto makeFont=[](int size,int weight) { return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI"); };
    font=makeFont(14,FW_NORMAL); smallFont=makeFont(12,FW_NORMAL); titleFont=makeFont(23,FW_SEMIBOLD); numberFont=makeFont(29,FW_SEMIBOLD);
    background=CreateSolidBrush(RGB(242,245,249)); white=CreateSolidBrush(RGB(255,255,255));
    wchar_t local[MAX_PATH]{}; GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
    settingsPath=std::wstring(local)+L"\\ADHostnameSearcher"; CreateDirectoryW(settingsPath.c_str(),nullptr); settingsPath+=L"\\settings.ini";
    wchar_t value[256]{}; GetPrivateProfileStringW(L"Naming",L"Prefix",L"",value,256,settingsPath.c_str());
    add(Prefix,L"EDIT",value,ES_AUTOHSCROLL); SendMessageW(controls[Prefix],EM_SETCUEBANNER,0,reinterpret_cast<LPARAM>(L"Ej.: ATZZTE"));
    GetPrivateProfileStringW(L"Naming",L"Fixed",L"000",value,256,settingsPath.c_str()); add(Fixed,L"EDIT",value,ES_AUTOHSCROLL);
    GetPrivateProfileStringW(L"Naming",L"Digits",L"3",value,256,settingsPath.c_str()); add(Digits,L"EDIT",value,ES_NUMBER);
    add(Range,L"COMBOBOX",L"",CBS_DROPDOWNLIST);
    for(auto s:{L"Automático: 0 al máximo en AD",L"Completo según los dígitos",L"Personalizado"}) SendMessageW(controls[Range],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
    SendMessageW(controls[Range],CB_SETCURSEL,0,0); add(From,L"EDIT",L"0",ES_NUMBER); add(To,L"EDIT",L"999",ES_NUMBER);
    add(Apply,L"BUTTON",L"Aplicar formato y rango",BS_PUSHBUTTON);
    add(Families,WC_LISTVIEWW,L"",LVS_REPORT|LVS_NOCOLUMNHEADER|LVS_SINGLESEL|WS_BORDER);
    ListView_SetExtendedListViewStyle(controls[Families],LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW familyColumn{}; familyColumn.mask=LVCF_WIDTH; familyColumn.cx=px(230); ListView_InsertColumn(controls[Families],0,&familyColumn);
    add(All,L"BUTTON",L"Todas",BS_PUSHBUTTON); add(None,L"BUTTON",L"Ninguna",BS_PUSHBUTTON);
    add(Refresh,L"BUTTON",L"Consultar AD",BS_PUSHBUTTON); add(Ping,L"BUTTON",L"Verificar ping + IP",BS_PUSHBUTTON); add(Cancel,L"BUTTON",L"Cancelar",BS_PUSHBUTTON);
    add(Details,L"BUTTON",L"Ver resumen",BS_PUSHBUTTON);
    add(Filter,L"EDIT",L"",ES_AUTOHSCROLL); SendMessageW(controls[Filter],EM_SETCUEBANNER,0,reinterpret_cast<LPARAM>(L"Nombre del equipo o dirección IPv4"));
    add(State,L"COMBOBOX",L"",CBS_DROPDOWNLIST);
    for(auto s:{L"Todos los estados",L"No registrados en AD",L"Registrados en AD",L"Conflictos de red"}) SendMessageW(controls[State],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));
    SendMessageW(controls[State],CB_SETCURSEL,0,0);
    add(Table,WC_LISTVIEWW,L"",LVS_REPORT|LVS_OWNERDATA|LVS_SHOWSELALWAYS|WS_BORDER);
    ListView_SetExtendedListViewStyle(controls[Table],LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    const wchar_t* names[]={L"Dispositivo",L"IP (ping IPv4)",L"Familia",L"Active Directory",L"Prueba de red"};
    int sizes[]={240,190,95,145,225};
    for(int i=0;i<5;++i) { LVCOLUMNW col{}; col.mask=LVCF_TEXT|LVCF_WIDTH; col.pszText=const_cast<LPWSTR>(names[i]); col.cx=px(sizes[i]); ListView_InsertColumn(controls[Table],i,&col); }
    add(Empty,L"STATIC",L"Su inventario aparecerá aquí.\nConfigure el formato y consulte Active Directory.",SS_CENTER);
    busy(false); SetTimer(window,1,150,nullptr);
}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: window=h; init(); return 0;
    case WM_SIZE: width=MulDiv(LOWORD(lp),96,dpi); height=MulDiv(HIWORD(lp),96,dpi); if(controls.size()) layout(); return 0;
    case WM_GETMINMAXINFO: { auto* limits=reinterpret_cast<MINMAXINFO*>(lp); limits->ptMinTrackSize={px(1120),px(830)}; return 0; }
    case WM_PAINT: paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: { auto dc=reinterpret_cast<HDC>(wp); SetBkMode(dc,TRANSPARENT); return reinterpret_cast<LRESULT>(white); }
    case WM_COMMAND:
        try {
            int id=LOWORD(wp),event=HIWORD(wp);
            if(id==Cancel && job) { job->cancel=true; status=L"Cancelando: esperando las operaciones de red en curso…"; invalidate(); return 0; }
            if(job) return 0;
            if(id==Refresh && event==BN_CLICKED) startDirectory();
            else if(id==Details && event==BN_CLICKED) showDetails();
            else if(id==Ping && event==BN_CLICKED) startPing();
            else if(id==Apply && event==BN_CLICKED) { rebuild(); busy(false); }
            else if((id==All || id==None) && event==BN_CLICKED) {
                loadingFamilies=true; for(size_t i=0;i<families.size();++i) ListView_SetCheckState(controls[Families],static_cast<int>(i),id==All);
                loadingFamilies=false; filterRows();
            } else if((id==Filter && event==EN_CHANGE) || (id==State && event==CBN_SELCHANGE)) filterRows();
            else if(id==Range && event==CBN_SELCHANGE) busy(false);
        } catch(const std::exception& e) { if(job && !job->thread.joinable()) job.reset(); busy(job!=nullptr); message(e.what()); }
        return 0;
    case WM_NOTIFY: {
        auto* header=reinterpret_cast<NMHDR*>(lp);
        if(header->idFrom==Families && header->code==LVN_ITEMCHANGED && !loadingFamilies) filterRows();
        if(header->idFrom==Table && header->code==LVN_GETDISPINFOW) {
            auto* info=reinterpret_cast<NMLVDISPINFOW*>(lp);
            if((info->item.mask&LVIF_TEXT) && info->item.iItem>=0 && static_cast<size_t>(info->item.iItem)<visible.size()) {
                auto s=cell(rows[visible[info->item.iItem]],info->item.iSubItem);
                lstrcpynW(info->item.pszText,s.c_str(),info->item.cchTextMax);
            }
        }
        if(header->idFrom==Table && header->code==NM_CUSTOMDRAW) {
            auto* draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT && draw->nmcd.dwItemSpec<visible.size()) {
                const auto& row=rows[visible[draw->nmcd.dwItemSpec]];
                draw->clrTextBk=draw->nmcd.dwItemSpec%2 ? RGB(247,249,252) : RGB(255,255,255);
                draw->clrText= !row.inAD && row.network==ad::Network::Responding ? RGB(178,62,33) : RGB(35,53,75);
                return CDRF_NEWFONT;
            }
        }
        return 0;
    }
    case WM_TIMER:
        if(job && job->done) {
            job->thread.join();
            bool wasCancelled=job->cancel;
            auto error=job->error;
            try {
                if(error.empty() && !wasCancelled) {
                    if(job->directory) { computers=std::move(job->computers); loaded=true; rebuild(); }
                    else { rows=std::move(job->rows); filterRows(); status=L"Prueba finalizada. IP resueltas por DNS; respuesta comprobada mediante ICMP IPv4."; }
                } else status=wasCancelled ? L"Operación cancelada. Se conservan los resultados anteriores." : L"No se pudo completar la operación. Los resultados anteriores no se actualizaron.";
            } catch(const std::exception& e) { error=e.what(); loaded=false; rows.clear(); visible.clear(); ListView_SetItemCount(controls[Table],0); status=L"No se pudo generar el informe. Revise el formato y el rango."; }
            job.reset(); busy(false); invalidate();
            if(closing) DestroyWindow(window); else if(!error.empty()) message(error);
        } else if(job && !job->directory && !job->cancel) {
            status=L"Verificando red: "+std::to_wstring(job->progress.load())+L" / "+std::to_wstring(job->total)+L" · 30 tareas simultáneas como máximo"; invalidate();
        }
        return 0;
    case WM_CLOSE:
        if(job) { closing=true; job->cancel=true; status=L"Cerrando: esperando las operaciones de red en curso…"; invalidate(); }
        else DestroyWindow(h);
        return 0;
    case WM_DESTROY: KillTimer(h,1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    WSADATA data{};
    if(WSAStartup(MAKEWORD(2,2),&data)!=0) { MessageBoxW(nullptr,L"No se pudo iniciar la red de Windows.",L"Error",MB_ICONERROR); return 1; }
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    WNDCLASSEXW cls{}; cls.cbSize=sizeof(cls); cls.lpfnWndProc=procedure; cls.hInstance=instance;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION); cls.lpszClassName=L"ADHostnameSearcherWindow";
    RegisterClassExW(&cls);
    HDC screen=GetDC(nullptr); dpi=GetDeviceCaps(screen,LOGPIXELSX); ReleaseDC(nullptr,screen);
    HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"AD Hostname Searcher",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,px(1240),px(900),nullptr,nullptr,instance,nullptr);
    if(!h) { WSACleanup(); return 1; }
    ShowWindow(h,show); UpdateWindow(h);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) if(!IsDialogMessageW(h,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    for(auto f:{font,titleFont,numberFont,smallFont}) DeleteObject(f);
    DeleteObject(background); DeleteObject(white); WSACleanup();
    return static_cast<int>(msg.wParam);
}
