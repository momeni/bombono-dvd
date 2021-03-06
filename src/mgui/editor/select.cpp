//
// mgui/editor/select.cpp
// This file is part of Bombono DVD project.
//
// Copyright (c) 2008-2010 Ilya Murav'jov
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
// 

#include <mgui/_pc_.h>

#include "select.h"

#include "kit.h"
#include "bind.h"
#include "render.h"
#include "actions.h"
#include "text.h"
#include "toolbar.h"
#include "visitors.h"
#include "fe-select.h"

#include <mgui/win_utils.h>
#include <mgui/key.h>
#include <mgui/project/handler.h>
#include <mgui/sdk/menu.h>  // Popup()
#include <mgui/sdk/widget.h>  // SetColor()
#include <mgui/gettext.h>
#include <mgui/dialog.h>
#include <mgui/sdk/treemodel.h>
#include <mgui/prefs.h>

#include <mbase/resources.h> // DataDirPath()

//typedef boost::function<bool(Comp::MediaObj*)> CMFunctor;
//
//static void ForeachSelectedCM(MEditorArea& edt_area, const CMFunctor& fnr)
//{
//    MenuRegion& m_rgn = edt_area.CurMenuRegion();
//    Comp::ListObj::ArrType& lst = m_rgn.List();
//    const int_array sel_arr     = edt_area.SelArr();
//    for( int i=0; i<(int)sel_arr.size(); ++i )
//        if( Comp::MediaObj* obj = dynamic_cast<Comp::MediaObj*>(lst[sel_arr[i]]) )
//            if( fnr(obj) )
//               break;
//}

static Comp::MediaObj* GetObj(int i)
{
    return CurMenuRegion().List()[i];
}

MediaObjRange SelectedMediaObjs()
{
    return fe::make_any( MenuEditor().SelArr() | fe::transformed(GetObj) );
}

static void SetCursorForEdt(SelActionType typ, Gtk::Widget& wdg);

void SelectState::ChangeState(MEditorArea& edt_area, SelectState& stt)
{
    edt_area.MEdt::SelectData::ChangeState(&stt);
}

void FrameRectListVis::VisitMediaObj(Comp::MediaObj& m_obj)
{
    if( IsInArray(objPos, posArr) )
        DrawGrabFrame(rctDrw, CalcRelPlacement(m_obj.Placement()));
}

void  FrameRectListVis::Visit(FrameThemeObj& fto)
{
    VisitMediaObj(fto);
}

void  FrameRectListVis::Visit(TextObj& t_obj)
{
    VisitMediaObj(t_obj);
}

namespace 
{
struct GetOddMatches
{
    int_array& dstArr;
          int  val;
         bool  isNew;
               GetOddMatches(int_array& dst_arr): 
                   dstArr(dst_arr), val(-1), isNew(true) { }
         void  operator() (int i)
               {
                   if( val != -1 )
                   {
                       bool is_diff = val != i;

                       if( is_diff && isNew )
                           dstArr.push_back(val);
                       isNew = is_diff;
                   }
                   val = i;
               }
};
}

// найти разницу между src_arr и dst_arr и результат положить в dst_arr
bool CalcDifference(int_array& dst_arr, const int_array& src_arr)
{
    int_array all_arr(src_arr);
    // сливаем в один
    all_arr.insert(all_arr.end(), dst_arr.begin(), dst_arr.end());
    std::sort(all_arr.begin(), all_arr.end());
    // те, которые встречаются ровно один раз и составляют разницу
    dst_arr.clear();
    GetOddMatches lst_mtch = std::for_each(all_arr.begin(), all_arr.end(), GetOddMatches(dst_arr));
    if( lst_mtch.isNew && lst_mtch.val != -1 )
    {
        // последний вручную надо добавить
        dst_arr.push_back(lst_mtch.val);
    }
    return dst_arr.size() != 0;
}

static void RenderByRLV(MEditorArea& edt_area, RectListVis& vis)
{
    edt_area.CurMenuRegion().Accept(vis);
    RenderForRegion(edt_area, vis.RectList());
}

void RenderFrameDiff(MEditorArea& edt_area, int_array& old_lst)
{
    if( CalcDifference(old_lst, edt_area.SelArr()) )
    {
        FrameRectListVis f_vis(old_lst);
        RenderByRLV(edt_area, f_vis);
    }
}

void ClearRenderFrames(MEditorArea& edt_area)
{
    int_array lst;
    edt_area.SelArr().swap(lst);
    RenderFrameDiff(edt_area, lst);
}

// определение действия по координатам курсора
class CursorActionVis: public SelVis
{
    typedef SelVis MyParent;
    public:

        SelActionType cursTyp;
                 int  mvPos; // в отличие от selPos, позиция mvPos будет не -1, если только над
                             // выделенным объектом
     const int_array& selArr;

                  CursorActionVis(int x, int y, const int_array& sel_arr) : MyParent(x, y), 
                      cursTyp(sctNORMAL), mvPos(-1), selArr(sel_arr) { }

   virtual  void  MakeCalcs(const Rect& rel_plc);

    protected:

        Rect extRct;
        Rect intRct;
};

static bool SetupCurFrame(FrameThemeObj* obj, MenuRegion&)
{
    const FrameTheme& theme = obj->Theme();
    if( theme != Editor::GetActiveTheme() )
    {
        Gtk::ComboBox& combo = MenuToolbar().frame_combo;
        //Gtk::TreeModel::Children children = combo.get_model()->children();
        //for( Gtk::TreeModel::iterator itr = children.begin(), end = children.end();
        //     itr != end; ++itr )
        boost_foreach( const Gtk::TreeRow& row, combo.get_model()->children() )
            if( Editor::Iter2FT(row) == theme )
            {
                combo.set_active(row);
                break;
            }
    }
    return false;
}

static bool SetupCurFont(TextObj* obj, MenuRegion&)
{
    const Pango::FontDescription& dsc = obj->FontDesc();
    Editor::Toolbar& tbar = MenuToolbar();
    // * 
    tbar.fontFmlEnt.get_entry()->set_text(dsc.get_family());
    // *
    str::stream str_strm;
    str_strm.precision(Editor::FONT_SZ_PRECISION);
    std::string sz_str = (str_strm << MEdt::FontSize(dsc)).str();
    tbar.fontSzEnt.get_entry()->set_text(sz_str);
    // *
    tbar.bldBtn.set_active(dsc.get_weight() == Pango::WEIGHT_BOLD);
    tbar.itaBtn.set_active(dsc.get_style() == Pango::STYLE_ITALIC);
    tbar.undBtn.set_active(obj->Style().isUnderlined);
    // *
    SetColor(tbar.clrBtn, obj->Color());
    return false;
}

static void DoSingleSelect(MEditorArea& edt_area, int pos)
{
    edt_area.ClearSel();
    edt_area.SelObj(pos);
}

// по первым найденным устанавливаем поля тулбара
static void UpdateToolbar(bool only_font = false)
{
    Editor::FTOFunctor fto_fnr = only_font ? Editor::FTOFunctor() : &SetupCurFrame;
    Editor::ForAllSelected(fto_fnr, &SetupCurFont);
}

static void DoSelection(MEditorArea& edt_area, NormalSelect::Data& dat, bool composite_select = true)
{
    // 0 сохраняем старый список
    int_array lst = edt_area.SelArr();

    // 1 меняем
    if( dat.curPos != -1 )
    {
        if( composite_select )
        {
            if( dat.isAdd )
            {
                edt_area.IsSelObj(dat.curPos)
                    ? edt_area.UnSelObj(dat.curPos)
                    : edt_area.SelObj(dat.curPos) ;
            }
            else
                DoSingleSelect(edt_area, dat.curPos);

            dat.curTyp = edt_area.IsSelObj(dat.curPos) ? sctMOVE : sctNORMAL ;
        }
        else
        {
            // простой вариант выбора
            if( !edt_area.IsSelObj(dat.curPos) )
                DoSingleSelect(edt_area, dat.curPos);
        }
    }
    else
        edt_area.ClearSel();

    // 2 находим разницу
    RenderFrameDiff(edt_area, lst);
    // 3 курсор
    SetCursorForEdt(dat.curTyp, edt_area);

    // * 
    UpdateToolbar();
}

void SelectPress::OnPressDown(MEditorArea& edt_area, NormalSelect::Data& dat)
{
    DoSelection(edt_area, dat);
}

void MovePress::OnPressUp(MEditorArea& edt_area, NormalSelect::Data& dat)
{
    DoSelection(edt_area, dat);
}

static void DeleteSelObjects();

static Comp::MediaObj* GetCurMO()
{
    //Project::MediaItem res_mi;
    //if( is_background )
    //    res_mi = CurMenuRegion().BgRef();
    //else
    //    boost_foreach( Comp::MediaObj* obj, SelectedMediaObjs() )
    //    {
    //        res_mi = obj->MediaItem();
    //        break;
    //    }
    Comp::MediaObj* res = 0;
    boost_foreach( Comp::MediaObj* obj, SelectedMediaObjs() )
    {
        res = obj;
        break;
    }
    return res;
}

// sel_pos - над каким элементом находимся
// dat.curPos - позиция выделенного элемента над курсором
static NormalSelect::Data& 
CalcMouseForData(MEditorArea& edt_area, GdkEventButton* event, int& sel_pos)
{
    CursorActionVis s_vis((int)event->x, (int)event->y, edt_area.SelArr());
    edt_area.CurMenuRegion().Accept(s_vis);

    sel_pos = s_vis.selPos; // над каким 

    NormalSelect::Data& dat = edt_area;
    ASSERT( !dat.prssStt );
    dat.msCoord = s_vis.lct;
    dat.curTyp  = s_vis.cursTyp;
    dat.curPos  = s_vis.mvPos;
    dat.isAdd = bool(event->state&GDK_CONTROL_MASK);
    return dat;
}

static void SetObjectsLinksEx(MEditorArea& edt_area, Project::MediaItem mi, const int_array& items,
                              bool for_poster)
{
    ClearLinkVis sr_vis(items, mi, for_poster);
    RenderByRLV(edt_area, sr_vis);
}

void SetLinkForObject(MEditorArea& edt_area, Project::MediaItem mi, int pos, bool for_poster)
{
    int_array single;
    single.push_back(pos);
    SetObjectsLinksEx(edt_area, mi, single, for_poster);
}

static void SetObjectsLinks(Project::MediaItem mi, bool for_poster)
{
    MEditorArea& edt_area = MenuEditor();
    SetObjectsLinksEx(edt_area, mi, edt_area.SelArr(), for_poster);
}

static void SetPlayAllFlag(bool is_on)
{
    boost_foreach( Comp::MediaObj* obj, SelectedMediaObjs() )
        obj->PlayAll() = is_on;
}

static void SetActionLinkImpl(Project::MediaItem mi)
{
    SetObjectsLinks(mi, false);
}

static void SetActionLink(Project::MediaItem mi)
{
    SetActionLinkImpl(mi);
    SetPlayAllFlag(false);
}

static void SetPlayAll()
{
    SetActionLinkImpl(Project::MediaItem());
    SetPlayAllFlag(true);
}

class LinkMenuBuilder: public Project::EditorMenuBuilder
{
    typedef Project::EditorMenuBuilder MyParent;
    public:
                    LinkMenuBuilder(Comp::MediaObj* obj)
                        :MyParent(obj->MediaItem(), MenuEditor(), false), playAll(obj->PlayAll()) {}

    virtual ActionFunctor  CreateAction(Project::MediaItem mi);
    virtual          void  AddConstantChoice();

    protected:
        bool  playAll;
};

ActionFunctor MakeActionLinker(Project::MediaItem mi)
{
    return bb::bind(&SetActionLink, mi);
}

ActionFunctor LinkMenuBuilder::CreateAction(Project::MediaItem mi)
{
    return MakeActionLinker(mi);
}

void LinkMenuBuilder::AddConstantChoice()
{
    AddNoLinkItem(*this, !playAll);
    AddPredefinedItem(_("Play All"), playAll, SetPlayAll);
}

class PosterMenuBuilder: public Project::EditorMenuBuilder
{
    typedef Project::EditorMenuBuilder MyParent;
    public:
    PosterMenuBuilder(Project::MediaItem cur_itm, MEditorArea& ed, bool is_back)
        :MyParent(cur_itm, ed, true), isBack(is_back) {}

    virtual ActionFunctor CreateAction(Project::MediaItem mi);

    protected:
        bool  isBack;
};

static void OnPosterChoice(Project::MediaItem mi, bool is_back)
{
    if( is_back )
        SetBackgroundLink(mi);
    else
        SetObjectsLinks(mi, true);
}

ActionFunctor PosterMenuBuilder::CreateAction(Project::MediaItem mi)
{
    // ко времени выбора "builder" меню уже не существует
    return bb::bind(&OnPosterChoice, mi, isBack);
}

typedef boost::function<void(Comp::MediaObj*)> CMFunctor2;

class AlignVis: public CommonDrawVis
{
    typedef CommonDrawVis MyParent;
    public:
        CMFunctor2  alignFnr;

                  AlignVis(const CMFunctor2& fnr, const int_array& sel_arr)
                    : MyParent(sel_arr), alignFnr(fnr) { }

   virtual  void  Visit(FrameThemeObj& fto) { RepositionObj(fto, MakeFTOMoving(fto)); }
   virtual  void  Visit(TextObj& t_obj)     { RepositionObj(t_obj, MakeTextMoving(t_obj)); }

            void  RepositionObj(Comp::MediaObj& m_obj, Manager ming);
};

void AlignVis::RepositionObj(Comp::MediaObj& m_obj, Manager ming)
{
    if( IsObjSelected() )
    {
        Draw(ming);
        alignFnr(&m_obj);
        Draw(ming);
    }
}

static void LeftAlignImpl(Comp::MediaObj* m_obj, const Rect& edge_rct)
{
    Rect rct = m_obj->Placement(); 
    m_obj->SetPlacement(rct + Point(edge_rct.lft-rct.lft, 0));
}

static void RightAlignImpl(Comp::MediaObj* m_obj, const Rect& edge_rct)
{
    Rect rct = m_obj->Placement(); 
    m_obj->SetPlacement(rct + Point(edge_rct.rgt-rct.rgt, 0));
}

static void TopAlignImpl(Comp::MediaObj* m_obj, const Rect& edge_rct)
{
    Rect rct = m_obj->Placement(); 
    m_obj->SetPlacement(rct + Point(0, edge_rct.top-rct.top));
}

static void BottomAlignImpl(Comp::MediaObj* m_obj, const Rect& edge_rct)
{
    Rect rct = m_obj->Placement(); 
    m_obj->SetPlacement(rct + Point(0, edge_rct.btm-rct.btm));
}

static void CenterHzImpl(Comp::MediaObj* m_obj, const Rect& edge_rct)
{
    m_obj->SetPlacement(CenterRect(m_obj->Placement(), edge_rct, true, false));
}

static void CenterVrImpl(Comp::MediaObj* m_obj, const Rect& edge_rct)
{
    m_obj->SetPlacement(CenterRect(m_obj->Placement(), edge_rct, false, true));
}

// to calculate the convex hull of non-empty set of rect's
struct EdgeCalcer
{
    bool  isFirst;
    Rect& edgeRct;
    
    EdgeCalcer(Rect& edge_rct): isFirst(true), edgeRct(edge_rct) {}
    ~EdgeCalcer()
    {
        ASSERT( !isFirst );
    }

    void Update(const Rect& rct)
    {
        edgeRct.lft = isFirst ? rct.lft : std::min(rct.lft, edgeRct.lft);
        edgeRct.rgt = isFirst ? rct.rgt : std::max(rct.rgt, edgeRct.rgt);
        edgeRct.top = isFirst ? rct.top : std::min(rct.top, edgeRct.top);
        edgeRct.btm = isFirst ? rct.btm : std::max(rct.btm, edgeRct.btm);

        isFirst = false;
    }
};

Rect ConvexHull(const fe::range<Comp::MediaObj*>& lst)
{
    Rect edge_rct;
    //ForeachSelectedCM(edt_area, bb::bind(&CalcAlignSettings, _1, boost::ref(edge_rct), boost::ref(is_first)));
    EdgeCalcer ec(edge_rct);
    boost_foreach( Comp::MediaObj* obj, lst )
        ec.Update(obj->Placement());
    return edge_rct;
}

typedef boost::function<void(Comp::MediaObj*, const Rect&)> CMFunctor3;

static void AlignByFunctor(const CMFunctor3& fnr)
{
    MEditorArea& edt_area = MenuEditor();
    Rect edge_rct = ConvexHull(SelectedMediaObjs());

    AlignVis vis(bb::bind(fnr, _1, edge_rct), edt_area.SelArr());
    RenderByRLV(edt_area, vis);
}

//Adds an Align Menu Item to Context Menu
static void AddAlignItem(Gtk::Menu& menu, const CMFunctor3& fnr, const char* name)
{
    Gtk::MenuItem& itm = MakeAppendMI(menu, name);
    itm.signal_activate().connect(bb::bind(&AlignByFunctor, fnr));
}

static RectListRgn CalcSelRegion(MEditorArea& edt_area)
{
    MenuRegion& rgn = edt_area.CurMenuRegion();
    SelRectVis sr_vis(edt_area.SelArr());
    rgn.Accept(sr_vis);
    return sr_vis.RectList();
}

static bool SortHzOrVr(Comp::MediaObj* a, Comp::MediaObj* b, bool is_hz)
{
    return is_hz ?
        a->Placement().lft < b->Placement().lft
    :
        a->Placement().top < b->Placement().btm;
}

static int LinearSize(const Rect& rct, bool is_hz)
{
    return (is_hz ? rct.Width() : rct.Height());
}

static void DistributeImpl(bool is_hz)
{
    MEditorArea& edt_area = MenuEditor();
    int sum_size = 0;
    std::vector<Comp::MediaObj*> m_objs;

    // tally sum of widths
    Rect edge_rct;
    {
        EdgeCalcer ec(edge_rct);
        boost_foreach( Comp::MediaObj* m_obj, SelectedMediaObjs() )
        {
            m_objs.push_back(m_obj);
    
            Rect plc = m_obj->Placement();
            sum_size += LinearSize(plc, is_hz);
            ec.Update(plc);
        }
    }
    // now have what's needed to distribute
    int distr_amt = (LinearSize(edge_rct, is_hz) - sum_size) / int(m_objs.size() - 1);

    RectListRgn old_rlr = CalcSelRegion(edt_area);
    // only distribute if there is room to do such, the dis_amt must be greater than zero
    // why? let us distribute always
    //if( distr_amt > 0 )
    {
        std::sort(m_objs.begin(), m_objs.end(), bb::bind(&SortHzOrVr, _1, _2, is_hz));
        // first object
        Rect f_plc = m_objs[0]->Placement();
        int next_pos = (is_hz ? f_plc.rgt : f_plc.btm) + distr_amt;
        // move objects in slice [1, -1]
        boost_foreach( Comp::MediaObj* m_obj, m_objs | fe::sliced(1, m_objs.size()-1) )
        {
            Rect plc = m_obj->Placement();

            Point move_vector = is_hz ? Point(next_pos - plc.lft, 0) : Point(0, next_pos - plc.top);
            m_obj->SetPlacement(plc + move_vector);

            next_pos += LinearSize(plc, is_hz) + distr_amt;
        }
    }
    RectListRgn new_rlr = CalcSelRegion(edt_area);

    // join regions & redraw
    new_rlr.insert(new_rlr.end(), old_rlr.begin(), old_rlr.end());
    RenderForRegion(edt_area, new_rlr);
}

bool SetEnabled(Gtk::Widget& wdg, bool is_enabled)
{
    wdg.set_sensitive(is_enabled);
    return is_enabled;
}

static void ReRenderAll(MEditorArea& edt_area)
{
    RenderForRegion(edt_area, Rect0Sz(edt_area.FramePlacement().Size()));
}

void SetBackgroundLink(Project::MediaItem mi)
{
    MEditorArea& edt_area = MenuEditor();
    MenuRegion& mr = edt_area.CurMenuRegion();
    mr.BgRef().SetLink(mi);
    ResetBackgroundImage(mr);
    
    ReRenderAll(edt_area);
}

//static void SetBgColor()
//{
//    Gtk::ColorSelectionDialog dlg(_("Set Background Color..."));
//    Gtk::ColorSelection& sel = *dlg.get_colorsel();
//    RGBA::Pixel& bg_clr      = CurMenuRegion().BgColor();
//
//    sel.set_current_color(PixelToColor(bg_clr));
//    if( dlg.run() == Gtk::RESPONSE_OK )
//    {
//        bg_clr = RGBA::ColorToPixel(sel.get_current_color());
//        SetBackgroundLink(Project::MediaItem()); // очистка + перерисовка
//    }
//}

static void ShowBGDialog()
{
    Gtk::Window& dlg = MenuEditor().bgDlg.dlg;
    //if( !dlg.get_visible() )
    //    dlg.show();
    dlg.present();
}

namespace Editor {

// для Project::BackSpanType (заполнение, по размеру, растянуть)
const char* SpanTypes[] = { N_("Fill"), N_("Fit"), N_("Stretch") };

static void OnResponse(Gtk::Dialog& dlg, int resp)
{
    if( resp == Gtk::RESPONSE_CLOSE )
        dlg.hide();
}

static void OnBackSettingChanged()
{
    MEditorArea& edt_area = MenuEditor();
    BackgroundDialog& bg = edt_area.bgDlg;
    // защита от зацикливания по фокусу
    if( edt_area.CurMenu() && bg.userFocus )
    {
        Project::BackSettings& bs = CurMenuRegion().bgSet;
        bs.bsTyp  = (Project::BackSpanType)bg.styleCombo.get_active_row_number();
        bs.sldClr = GetColor(bg.clrBtn);
        
        ReRenderAll(edt_area);
    }
}

typedef Gtk::TreeModelColumn<std::string> StrColumn;

struct IHSFields
{
    StrColumn  uriCln;

    IHSFields(Gtk::TreeModelColumnRecord& rec)
    {
        rec.add(uriCln);
    }
};

static StrColumn& UriColumn()
{
    return GetColumnFields<IHSFields>().uriCln;
}

static void OnIHSGo(Gtk::ComboBox& combo)
{
    std::string uri = combo.get_active()->get_value(UriColumn());
    GoUrl(uri.c_str());
}

BackgroundDialog::BackgroundDialog(): 
    dlg(_("Background Settings")), userFocus(true)
{
    SetDialogStrict(dlg, 350, -1);
    DialogVBox& vbox = AddHIGedVBox(dlg);
    
    Gtk::ComboBoxText& st_cmb = styleCombo;
    for( int i=0; i<(int)ARR_SIZE(SpanTypes); i++ )
        st_cmb.append_text(gettext(SpanTypes[i]));
    AppendWithLabel(vbox, st_cmb, SMCLN_("_Style"));
    
    AppendWithLabel(vbox, clrBtn, SMCLN_("_Color"));
    
    // * интерактив
    st_cmb.signal_changed().connect(&OnBackSettingChanged);
    clrBtn.signal_color_set().connect(&OnBackSettingChanged);
    
    // * интеграция с сервисами картинок
    StrColumn nam_cln;
    Gtk::TreeModelColumnRecord& columns = GetColumnRecord<IHSFields>();
    columns.add(nam_cln);
    RefPtr<Gtk::ListStore> ihs_store = Gtk::ListStore::create(columns);
    Gtk::ComboBox& combo = ihsCmb; //NewManaged<Gtk::ComboBox>(ihs_store);
    combo.set_model(ihs_store);

    // :TODO: refactor
    io::stream strm(DataDirPath("ihslist.lst").c_str(), iof::in);
    std::string line_str;
    for( int i=0; std::getline(strm, line_str); i++ )
    {
        const char* name  = line_str.c_str();
        const char* space = strchr(name, ' ');
        std::string uri   = space ? std::string(name, space) : line_str ;
        std::string title = std::string(space ? space+1 : name);

        Gtk::TreeRow row = *ihs_store->append();
        row[UriColumn()] = uri;
        row[nam_cln] = title;
        if( i == UnnamedPrefs().ihsNum )
            combo.set_active(i);
    }

    Gtk::VBox& vb = PackStart(vbox, NewManaged<Gtk::VBox>());
    PackStart(vb, LabelForWidget(SMCLN_("Go to online image service in web browser"), combo));
    Gtk::HBox& hbox = PackCompositeWdg(combo);
    SetTip(hbox, _("You can drop background image you like from a web browser directly onto Menu Editor"));
    combo.pack_start(nam_cln);
    PackCompositeWdgButton(hbox, Gtk::Stock::APPLY, bb::bind(&OnIHSGo, b::ref(combo)));
    PackStart(vb, hbox);

    // сам по умолчанию скрыт
    CompleteDialog(dlg, true);
    dlg.signal_response().connect(bb::bind(&OnResponse, b::ref(dlg), _1));
}

BackgroundDialog::~BackgroundDialog()
{
    UnnamedPrefs().ihsNum = ihsCmb.get_active_row_number();
}

} // namespace Editor

FrameThemeObj* ToRealFTOTransform(int i) 
{
    FrameThemeObj* obj = dynamic_cast<FrameThemeObj*>(GetObj(i));
    if( obj && IsIconTheme(*obj) )
        obj = 0;
    return obj;
}

static bool IsNotNull(const FrameThemeObj* fto)
{
    return fto;
}

fe::range<FrameThemeObj*> SelectedRealFTOs()
{
    return fe::make_any( MenuEditor().SelArr() | fe::transformed(ToRealFTOTransform) | fe::filtered(IsNotNull) );
}

static void OnHighlightBorder(Gtk::CheckMenuItem& hib_itm)
{
    bool is_on = hib_itm.get_active();
    boost_foreach( FrameThemeObj* obj, SelectedRealFTOs() )
        obj->hlBorder = is_on;
}

void AddImageItem(Gtk::Menu& menu, const Gtk::StockID& id, const ActionFunctor& fnr, bool is_enabled)
{
    Gtk::MenuItem& itm = AppendMI(menu, NewManaged<Gtk::ImageMenuItem>(id));
    itm.set_sensitive(is_enabled);
    itm.signal_activate().connect(fnr);
}

void NormalSelect::OnMouseDown(MEditorArea& edt_area, GdkEventButton* event)
{
    int sel_pos;
    NormalSelect::Data& dat = CalcMouseForData(edt_area, event, sel_pos);

    if( IsLeftButton(event) )
    {
//         CursorActionVis s_vis((int)event->x, (int)event->y, edt_area.SelArr());
//         edt_area.CurMenuRegion().Accept(s_vis);
//
//         NormalSelect::Data& dat = edt_area;
//         ASSERT( !dat.prssStt );
//         dat.msCoord = s_vis.lct;
//         dat.curTyp  = s_vis.cursTyp;
//         dat.curPos  = s_vis.mvPos;
//         dat.isAdd = bool(event->state&GDK_CONTROL_MASK);

        if( dat.curPos != -1 ) // двигаем
            dat.prssStt = new MovePress;
        else
        {
            dat.prssStt = new SelectPress;
            dat.curPos  = sel_pos; //s_vis.selPos;
        }
        dat.prssStt->OnPressDown(edt_area, dat);
    }
    else if( IsRightButton(event) )
    {
        dat.curPos = sel_pos;
        DoSelection(edt_area, dat, false);

        using namespace Gtk::Menu_Helpers;
        const int_array& sel_arr = edt_area.SelArr();
        bool has_selected = !sel_arr.empty();
        Gtk::Menu& mn     = NewPopupMenu(); 

        // :TODO: клавиатурные сочетания сделать + отобразить в меню
        AddImageItem(mn, Gtk::Stock::CUT,   bb::bind(&OnEditorCopy, true), has_selected);
        AddImageItem(mn, Gtk::Stock::COPY,  bb::bind(&OnEditorCopy, false), has_selected);
        AddImageItem(mn, Gtk::Stock::PASTE, bb::bind(&OnEditorPaste, Point(event->x, event->y)), CopyList.size());
        
        AddEnabledItem(mn, _("Delete"), &DeleteSelObjects, has_selected);
        AppendSeparator(mn);

        // Link
        //bool is_background = !has_selected;
        //Project::Menu cur_mn = edt_area.CurMenu();
        //Project::SetLinkMenu& slm = cur_mn->GetData<Project::SetLinkMenu>();
        //slm.isForBack = is_background;
        //slm.newLink   = GetCurObjectLink(is_background);
        //
        //InvokeOn(cur_mn, "SetLinkMenu");
        //if( slm.linkMenu )
        //{
        //    mn.items().push_back(MenuElem(_("Link")));
        //    mn.items().back().set_submenu(*slm.linkMenu.release());
        //}
        //mn.items().push_back(
        //    MenuElem(_("Remove Link"), bb::bind(&SetSelObjectsLinks,
        //                                        Project::MediaItem(), is_background)));
        Gtk::MenuItem& link_itm = MakeAppendMI(mn, _("Link"));
        if( SetEnabled(link_itm, has_selected) )
            link_itm.set_submenu(LinkMenuBuilder(GetCurMO()).Create());
        AddEnabledItem(mn, _("Remove Link"), MakeActionLinker(Project::MediaItem()),
                       has_selected);

        Project::MediaItem cur_pstr;
        bool has_fto = false;
        bool is_hib  = false;
        boost_foreach( FrameThemeObj* obj, SelectedRealFTOs() )
        {
            has_fto = true;
            is_hib  = obj->hlBorder;
            cur_pstr = obj->PosterItem();
            break;
        }
        if( !has_selected )
            cur_pstr = CurMenuRegion().BgRef();
                
        // Poster Link
        Gtk::MenuItem& poster_itm = MakeAppendMI(mn, _("Set Poster"));
        if( SetEnabled(poster_itm, (!has_selected || has_fto)) )
            poster_itm.set_submenu(PosterMenuBuilder(cur_pstr, edt_area, !has_selected).Create());

        Gtk::CheckMenuItem& hib_itm = NewManaged<Gtk::CheckMenuItem>(_("Highlight Border Only"));
        mn.append(hib_itm);
        if( SetEnabled(hib_itm, has_fto) )
        {    
            hib_itm.set_active(is_hib);
            hib_itm.signal_toggled().connect(bb::bind(&OnHighlightBorder, b::ref(hib_itm)));
        }
            
        // Align
        {
            Gtk::MenuItem& align_itm = MakeAppendMI(mn, _("Align"));
            if( SetEnabled(align_itm, has_selected) )
            {
                Gtk::Menu& menu = NewManaged<Gtk::Menu>();
                align_itm.set_submenu(menu);

                AddAlignItem(menu, LeftAlignImpl,   _("Align Left"));
                AddAlignItem(menu, RightAlignImpl,  _("Align Right"));
                AddAlignItem(menu, TopAlignImpl,    _("Align Top"));
                AddAlignItem(menu, BottomAlignImpl, _("Align Bottom"));
                AppendSeparator(menu);

                AddAlignItem(menu, CenterHzImpl, _("Center Horizontally"));
                AddAlignItem(menu, CenterVrImpl, _("Center Vertically"));

                //allow horizontal or vertical distribute if three or more objects selected
                bool can_distribute = ( sel_arr.size() > 2 );
                AppendSeparator(menu);
                AddEnabledItem(menu, _("Distribute Horizontally"), bb::bind(&DistributeImpl, true), can_distribute);
                AddEnabledItem(menu, _("Distribute Vertically"),  bb::bind(&DistributeImpl, false), can_distribute);
            }
        }

        // Фон
        //AddEnabledItem(mn, _("Set Background Color..."), &SetBgColor, !has_selected);
        AddEnabledItem(mn, DOTS_("Background Settings"), &ShowBGDialog, !has_selected);

        //mn.accelerate(edt_area);
        Popup(mn, event, true);
    }
}

void NormalSelect::ClearPress(MEditorArea& edt_area)
{
    NormalSelect::Data& dat = edt_area;
    ASSERT( dat.prssStt );
    dat.prssStt = 0;
}

void NormalSelect::OnMouseUp(MEditorArea& edt_area, GdkEventButton* event)
{
    if( !IsLeftButton(event) )
        return;

    NormalSelect::Data& dat = edt_area;
    // бывают ситуации, когда приходит только MouseUp
    // например, при автодополнении список "накрывает" редактор, причем 
    // выбор происходит на MouseDown, за которым сразу же список и закрывается;
    // тогда MouseUp приходит тому, что под ним - редактору
    if( !dat.prssStt )
        return;
    dat.prssStt->OnPressUp(edt_area, dat);

    ClearPress(edt_area);
}


static void EnlargeRect(Rect& rct, int add)
{
    rct.lft -= add;
    rct.rgt += add;
    rct.top -= add;
    rct.btm += add;
}

void CursorActionVis::MakeCalcs(const Rect& rel_plc)
{
    MyParent::MakeCalcs(rel_plc);

    // 0 только выделенные
    //if( !menuRgn->IsSelObj(objPos) )
    if( !IsInArray(objPos, selArr) )
    {
        if( selPos == objPos )
        {
            cursTyp = sctNORMAL;
            mvPos = -1;
        }
        return;
    }

    Point sz = rel_plc.Size();
    // приращение
    int fram_spc = std::min( std::min( sz.x/20, sz.y/20 ), 10 );

    extRct = rel_plc;
    EnlargeRect(extRct, fram_spc);
    // далеко от объекта
    if( !extRct.Contains(lct) )
        return;

    mvPos = objPos;

    intRct = rel_plc;
    EnlargeRect(intRct, -fram_spc);
    // 1 передвижение
    if( intRct.Contains(lct) )
    {
        cursTyp = sctMOVE;
        return;
    }

    // таблица распределения областей указателей
    SelActionType section_tbl[16] = 
    {
        sctRESIZE_LT,   sctRESIZE_TOP, sctRESIZE_TOP, sctRESIZE_RT,
        sctRESIZE_LEFT, sctMOVE,       sctMOVE,       sctRESIZE_RIGHT,
        sctRESIZE_LEFT, sctMOVE,       sctMOVE,       sctRESIZE_RIGHT,
        sctRESIZE_LB,   sctRESIZE_BTM, sctRESIZE_BTM, sctRESIZE_RB
    };

    sz.x = (extRct.rgt-extRct.lft)/4 + 1;
    sz.y = (extRct.btm-extRct.top)/4 + 1;

    Point x_add(sz.x, 0);
    for( int i=0; i<4; i++ )
    {
        intRct.lft = extRct.lft;
        intRct.top = extRct.top + sz.y*i;
        intRct.rgt = intRct.lft + sz.x;
        intRct.btm = intRct.top + sz.y;
        for( int j=0; j<4; j++, intRct += x_add )
            if( intRct.Contains(lct) )
            {
                cursTyp = section_tbl[i*4+j];
                return;
            }
    }
    ASSERT(0);
}

static void SetCursorForEdt(SelActionType typ, Gtk::Widget& wdg)
{
    if( typ == sctNORMAL )
        SetCursorForWdg(wdg);
    else
    {
        using namespace Gdk;
        CursorType curs_tbl[9] = 
        {
            FLEUR,

            LEFT_TEE,
            RIGHT_TEE,
            TOP_TEE,
            BOTTOM_TEE,

            UL_ANGLE,  
            UR_ANGLE,
            LR_ANGLE,
            LL_ANGLE,  
        };
        Gdk::Cursor curs(curs_tbl[typ-sctMOVE]);
        SetCursorForWdg(wdg, &curs);
    }
}

void NormalSelect::OnMouseMove(MEditorArea& edt_area, GdkEventMotion* event)
{
    NormalSelect::Data& dat = edt_area;
    if( dat.prssStt )
    {
        MoveSelect* new_state = 0;
        switch( dat.curTyp )
        {
        case sctNORMAL:
            new_state = &NothingSelect::Instance();
            break;
        case sctMOVE:
            new_state = &RepositionSelect::Instance();
            break;
        default:
            new_state = &ResizeSelect::Instance();
        }
        new_state->InitData(dat, edt_area);

        ChangeState(edt_area, *new_state);
        new_state->OnMouseMove(edt_area, event);
    }

    CursorActionVis vis((int)event->x, (int)event->y, edt_area.SelArr());
    edt_area.CurMenuRegion().Accept(vis);
    SetCursorForEdt(vis.cursTyp, edt_area);
}

CommonDrawVis::Manager CommonDrawVis::MakeFTOMoving(FrameThemeObj& fto)
{
    return new MBind::FTOMoving(RectList(), fto, menuRgn->Transition());
}

CommonDrawVis::Manager CommonDrawVis::MakeTextMoving(TextObj& t_obj)
{
    return new MBind::TextMoving(RectList(), t_obj.GetData<EdtTextRenderer>());
}

void CommonDrawVis::Draw(Manager ming)
{
    ming->Redraw();
    DrawGrabFrame(rctDrw, ming->CalcRelPlacement());
}

void SelRectVis::RedrawMO(Manager ming)
{
    if( IsObjSelected() )
        Draw(ming); 
}

void AppendObjIndex(int_array& arr, Comp::MediaObj* obj, const ListObj::ArrType& lst)
{
    ListObj::ArrType::const_iterator beg = lst.begin(), end = lst.end();
    ListObj::ArrType::const_iterator it = std::find(beg, end, obj);
    ASSERT( it != end );
    arr.push_back(it - beg);
}

void DeleteObjects(const int_array& del_nums, RectListRgn& rct_lst)
{
    MEditorArea& edt_area = MenuEditor();
    typedef ListObj::ArrType ArrType;
    MenuRegion& rgn    = edt_area.CurMenuRegion();
    int_array& sel_arr = edt_area.SelArr();
    ArrType&       lst = rgn.List();
    // * узнаем где отрисовывать
    SelRectVis sr_vis(del_nums);
    rgn.Accept(sr_vis);
    // * удаляем объекты
    ArrType new_lst;
    ArrType new_sel_lst;
    for( int i=0; i<(int)lst.size(); ++i )
    {
        Comp::MediaObj* obj = lst[i];
        if( IsInArray(i, del_nums) )
            Destroy(obj);
        else
        {    
            new_lst.push_back(obj);
            if( IsInArray(i, sel_arr) )
                new_sel_lst.push_back(obj);
        }
    }
    lst.swap(new_lst);
    // * обновляем выделение
    sel_arr.clear();
    boost_foreach( Comp::MediaObj* obj, new_sel_lst )
        AppendObjIndex(sel_arr, obj, lst);
    
    rct_lst.swap(sr_vis.RectList());
}

static void DeleteSelObjects()
{
    MEditorArea& edt_area = MenuEditor();
    RectListRgn rct_lst;
    DeleteObjects(edt_area.SelArr(), rct_lst);
    RenderForRegion(edt_area, rct_lst);
}

void ClearFTOCache(FrameThemeObj& fto)
{
    if( IsIconTheme(fto) )
        ClearDwPixbuf(fto);
    else
        fto.GetData<FTOInterPixData>().ClearPix();
}

void ClearLinkVis::Visit(FrameThemeObj& fto)
{
    if( IsObjSelected() )
    {
        Project::MediaItem old_mi = Project::MIToDraw(fto);
        Project::GetFTOLink(fto, forPoster).SetLink(newMI);

        if( old_mi != Project::MIToDraw(fto) )
        {
            // нужна перерисовка
            ClearFTOCache(fto);
            MyParent::Visit(fto);
        }
    }
}

void ClearLinkVis::Visit(TextObj& t_obj)
{
    if( !forPoster && IsObjSelected() )
        t_obj.MediaItem().SetLink(newMI);
    //MyParent::Visit(t_obj);
}

class SelTextRefontVis: public CommonDrawVis
{
    typedef CommonDrawVis MyParent;
    public:
        const Editor::TextStyle& tStyle;
                           bool  onlyColor; 

                SelTextRefontVis(const int_array& sel_arr, const Editor::TextStyle& ts,
                                 bool only_clr)
                    : MyParent(sel_arr), tStyle(ts), onlyColor(only_clr) {}

 virtual  void  Visit(TextObj& t_obj);
};

void SelTextRefontVis::Visit(TextObj& t_obj)
{ 
    if( IsObjSelected() )
    {
        Manager ming = MakeTextMoving(t_obj);
        Draw(ming);

        if( onlyColor )
            t_obj.SetColor(tStyle.color);
        else
        {
            void ReStyle(TextObj& t_obj, const Editor::TextStyle& style);
            ReStyle(t_obj, tStyle);
            ming->Update();
    
            Draw(ming);
        }
    }
}

void SetSelObjectsTStyle(MEditorArea& edt_area, const Editor::TextStyle& ts,
                         bool only_clr)
{
    SelTextRefontVis vis(edt_area.SelArr(), ts, only_clr);
    RenderByRLV(edt_area, vis);
}

void NormalSelect::OnKeyPressEvent(MEditorArea& edt_area, GdkEventKey* event)
{
    NormalSelect::Data& dat = edt_area;
    if( dat.prssStt )
        return;

    // :TODO: "key_binding" - см. meditor_text.cpp
    if( CanShiftOnly(event->state) )
    {
        switch( event->keyval )
        {
        case GDK_Delete:  case GDK_KP_Delete:
            DeleteSelObjects();
            break;
        default:
            break;
        }
    }
}

bool NormalSelect::IsEndState(MEditorArea& edt_area)
{ 
    NormalSelect::Data& dat = edt_area;
    return !dat.prssStt;
}


void MoveSelect::OnMouseDown(MEditorArea&, GdkEventButton* event)
{
    if( !IsLeftButton(event) )
        return;
    // невозможно
    ASSERT(0);
}

void MoveSelect::OnMouseUp(MEditorArea& edt_area, GdkEventButton* event)
{
    // по выходу из области виджета X автоматом захватывает мышь, поэтому 
    // "грабить" мышь не надо (gdk_pointer_grab())
    if( !IsLeftButton(event) )
        return;

    NormalSelect& nrm_stt = NormalSelect::Instance();
    nrm_stt.ClearPress(edt_area);
    ChangeState(edt_area, nrm_stt);
}

void MoveSelect::InitData(const NormalSelect::Data& dat,  MEditorArea& edt_area)
{
    MoveSelect::Data& move_dat = edt_area;
    move_dat.curTyp = dat.curTyp;

    move_dat.origCoord = edt_area.Transition().RelToAbs(dat.msCoord);

    InitDataExt(dat, edt_area);
}

class MoveVis: public CommonDrawVis //RectListVis
{
    typedef CommonDrawVis MyParent;
    public:
                  MoveVis(int x, int y, Point& orig_pnt, const int_array& sel_arr)
                    : MyParent(sel_arr), lct(x, y), origPnt(orig_pnt) { }

    protected:

            Point  lct;
            Point  origPnt;

            void  CalcMoveVector();
};

void MoveVis::CalcMoveVector()
{
    lct = DevToAbs(menuRgn->Transition(), lct);
    // вектор перемещения
    lct = lct - origPnt;
}

// переместить выделенные объекты и получить набор областей для отрисовки
class RepositionVis: public MoveVis
{
    typedef MoveVis MyParent;
    public:
                  RepositionVis(int x, int y, Point& orig_pnt, const int_array& sel_arr)
                    : MyParent(x, y, orig_pnt, sel_arr) { }

   virtual  void  VisitImpl(MenuRegion& menu_rgn);
   virtual  void  Visit(FrameThemeObj& fto) { RepositionObj(fto, MakeFTOMoving(fto)); }
   virtual  void  Visit(TextObj& t_obj)     { RepositionObj(t_obj, MakeTextMoving(t_obj)); }

           Point  Shift() { return lct; }
            void  RepositionObj(Comp::MediaObj& m_obj, Manager ming);
};

static bool IsSnapToGrid()
{
    return IsSnapToGrid(MenuEditor());
}

static bool DoGridSnap(int& coord)
{
    bool res = true;
    int val = coord % GRID_STEP;
    const int gs3 = GRID_STEP / 3;
    
    if( val <= gs3 )
        coord -= val;
    else if( val >= GRID_STEP - gs3 )
        coord += GRID_STEP - val;
    else
        // нет прилипания
        res = false;
    return res;
}

typedef int Point::* PointAttr;

static void UpdateSnap(bool& has_snap, int new_shift, Point& lct, const Point& orig_lct,
                       PointAttr pa)
{
    if( !has_snap || (std::abs(new_shift - orig_lct.*pa) < std::abs(lct.*pa - orig_lct.*pa)) )
    {    
        lct.*pa = new_shift;
        has_snap = true;
    }
}

void RepositionVis::VisitImpl(MenuRegion& menu_rgn)
{
    if( menu_rgn.FramePlacement().Contains(lct) )
    {    
        CalcMoveVector();
        if( IsSnapToGrid() )
        {
            const Point orig_lct = lct;
            bool has_x_snap = false;
            bool has_y_snap = false;
            boost_foreach( Comp::MediaObj* obj, SelectedMediaObjs() )
            {
                const Rect& plc = obj->Placement();
                Rect nplc = plc + orig_lct;
                
                if( DoGridSnap(nplc.lft) )
                    UpdateSnap(has_x_snap, nplc.lft - plc.lft, lct, orig_lct, &Point::x);
                if( DoGridSnap(nplc.rgt) )
                    UpdateSnap(has_x_snap, nplc.rgt - plc.rgt, lct, orig_lct, &Point::x);
                
                if( DoGridSnap(nplc.top) )
                    UpdateSnap(has_y_snap, nplc.top - plc.top, lct, orig_lct, &Point::y);
                if( DoGridSnap(nplc.btm) )
                    UpdateSnap(has_y_snap, nplc.btm - plc.btm, lct, orig_lct, &Point::y);
                                
            }
        }
    }
    else
        lct = Point(0, 0); // за пределы редактора не хотим выходить

    MyParent::VisitImpl(menu_rgn);
}

void RepositionVis::RepositionObj(Comp::MediaObj& m_obj, Manager ming)
{
    if( IsObjSelected() && !lct.IsNull() )
    {
        Draw(ming);

        // новое положение
        Rect new_rct = m_obj.Placement(); 
        new_rct += lct;
        m_obj.SetPlacement(new_rct);

        Draw(ming);
    }
}

#if 0
// сохранить список (например, для проверки ReDivide)
static void SaveRectList(const RectListRgn& rct_lst)
{
    io::stream tmp_strm("../ttt_r", iof::out|iof::app);
    for( RLRIterCType cur = rct_lst.begin(), end = rct_lst.end(); cur != end; ++cur )
    {
        const Rect& rct = *cur;
        tmp_strm << "    r_arr.push_back(Rect("
                        << rct.lft << ", " << rct.top <<
                   ", " << rct.rgt << ", " << rct.btm << "));" << io::endl;
    }
    tmp_strm << "---------" << io::endl;
}
#endif

void RepositionSelect::OnMouseMove(MEditorArea& edt_area, GdkEventMotion* event)
{
    MoveSelect::Data& dat = edt_area;
    RepositionVis rp_vis((int)event->x, (int)event->y, dat.origCoord, edt_area.SelArr());
    //SaveRectList(rp_vis.RectList());
    RenderByRLV(edt_area, rp_vis);

    dat.origCoord = dat.origCoord + rp_vis.Shift();
}

class SetVectorVis: public CommonGuiVis
{
    typedef CommonGuiVis MyParent;
    public:

                   SetVectorVis(MoveSelect::Data& dat): mvDat(dat), 
                       isMakeVector(false) {}

             void  SetMakeVector(bool is_make_vector) { isMakeVector = is_make_vector; }
    virtual  void  Visit(FrameThemeObj& fto);
    virtual  void  Visit(TextObj& t_obj);

    protected:

        MoveSelect::Data& mvDat;
                    bool  isMakeVector;
                   Point  basVec;

             void  Visit(Comp::MediaObj& mo);
};

void GetBasePointExt(Point& res, const Rect& plc, SelActionType cur_typ, Point& dir_pnt)
{
    dir_pnt.x = 1;
    dir_pnt.y = 1;
    switch( cur_typ )
    {
    case sctRESIZE_LEFT:
        res.x = plc.rgt;
        dir_pnt.y = 0;
        break;
    case sctRESIZE_RIGHT:
        res.x = plc.lft;
        dir_pnt.y = 0;
        break;
    case sctRESIZE_TOP:
        res.y = plc.btm;
        dir_pnt.x = 0;
        break;
    case sctRESIZE_BTM:
        res.y = plc.top;
        dir_pnt.x = 0;
        break;

    case sctRESIZE_LT:
        res.x = plc.rgt;
        res.y = plc.btm;
        break;
    case sctRESIZE_RT:
        res.x = plc.lft;
        res.y = plc.btm;
        break;
    case sctRESIZE_RB:
        res.x = plc.lft;
        res.y = plc.top;
        break;
    case sctRESIZE_LB:
        res.x = plc.rgt;
        res.y = plc.top;
        break;
    default:
        ASSERT(0);
    }
}

struct SizeVectorData
{
    typedef std::pair<double, double> DblPoint;
    DblPoint  vecPnt;
      double  ratio;

              SizeVectorData(): vecPnt(0,0) {}

        void  Set(Point& sz, Point& bas_vec)
              {
                  vecPnt.first  = bas_vec.x ? (double)sz.x/bas_vec.x : 0 ;
                  vecPnt.second = bas_vec.y ? (double)sz.y/bas_vec.y : 0 ;
                  
                  ASSERT_RTL( sz.y );
                  ratio = sz.x / (double)sz.y;
              }
        void  CalcPlacement(Rect& plc, Point& lct);
};

static bool IsLftResize(SizeVectorData::DblPoint& vec_pnt)
{
    return vec_pnt.first < 0;
}

static bool IsRgtResize(SizeVectorData::DblPoint& vec_pnt)
{
    return vec_pnt.first > 0;
}

static bool IsTopResize(SizeVectorData::DblPoint& vec_pnt)
{
    return vec_pnt.second < 0;
}

static bool IsBtmResize(SizeVectorData::DblPoint& vec_pnt)
{
    return vec_pnt.second > 0;
}

static bool IsHzResize(SizeVectorData::DblPoint& vec_pnt)
{
    return vec_pnt.first;
}

static void CheckMinSz(int& len)
{
    len = std::max(len, 10);
}

void SizeVectorData::CalcPlacement(Rect& plc, Point& lct)
{
    // 1 размер
    Point sz = plc.Size();
    if( IsHzResize(vecPnt) ) sz.x  = int(vecPnt.first *lct.x);
    if( vecPnt.second ) sz.y = int(vecPnt.second*lct.y);

    CheckMinSz(sz.x);
    CheckMinSz(sz.y);

    // 2 lt-точка
    Point lt_pnt;
    lt_pnt.x = IsLftResize(vecPnt) ? plc.rgt - sz.x : plc.lft ;
    lt_pnt.y = IsTopResize(vecPnt) ? plc.btm - sz.y : plc.top ;

    // 3 - результат
    plc.lft = lt_pnt.x;
    plc.top = lt_pnt.y;
    plc.rgt = lt_pnt.x + sz.x;
    plc.btm = lt_pnt.y + sz.y;
    
    if( IsSnapToGrid() )
    {
        // :TRICKY: в отличие от перемещения объектов здесь привязка
        // идет по каждому объекту отдельно (а не группы), что не идеально;
        // однако последующая реализация фичи сохранения пропорций будет
        // конфиктовать с прилипанием => сохранение пропорций важнее, считаем =>
        // оставляем как есть
        
        if( IsLftResize(vecPnt) )
            DoGridSnap(plc.lft);
        if( IsRgtResize(vecPnt) )
            DoGridSnap(plc.rgt);
        if( IsTopResize(vecPnt) )
            DoGridSnap(plc.top);
        if( IsBtmResize(vecPnt) )
            DoGridSnap(plc.btm);
    }
    
    // сохранение пропорций с Shift
    if( GetKeyboardState() & GDK_SHIFT_MASK )
    {
        sz = plc.Size();

        if( IsHzResize(vecPnt) )
        {    
            int hgt = Round(sz.x / ratio);
            CheckMinSz(hgt);
            if( IsTopResize(vecPnt) )
                plc.top = plc.btm - hgt;
            else
                plc.btm = plc.top + hgt;
        }
        else
        {
            int wdh = Round(ratio * sz.y);
            CheckMinSz(wdh);
            if( IsLftResize(vecPnt) )
                plc.lft = plc.rgt - wdh;
            else
                plc.rgt = plc.lft + wdh;
        }
    }
}

void SetVectorVis::Visit(Comp::MediaObj& mo)
{
    const Rect& plc = mo.Placement();
    if( isMakeVector )
    {
        Point res_pnt, dir_pnt;
        GetBasePointExt(res_pnt, plc, mvDat.curTyp, dir_pnt);
        basVec.x = dir_pnt.x ? mvDat.origCoord.x - res_pnt.x : 0 ;
        basVec.y = dir_pnt.y ? mvDat.origCoord.y - res_pnt.y : 0 ;
        mvDat.origCoord = res_pnt;
    }
    else
    {
        ASSERT( !basVec.IsNull() && !plc.IsNull() );
        Point sz = plc.Size();

        SizeVectorData& svd = mo.GetData<SizeVectorData>();
        svd.Set(sz, basVec);
    }
}

void SetVectorVis::Visit(FrameThemeObj& fto)
{
    Visit((Comp::MediaObj&)fto);
}

void SetVectorVis::Visit(TextObj& t_obj)
{
    Visit((Comp::MediaObj&)t_obj);
}

void ResizeSelect::InitDataExt(const NormalSelect::Data& dat, MEditorArea& edt_area)
{
    MoveSelect::Data& move_dat = edt_area;
    MenuRegion& m_rgn = edt_area.CurMenuRegion();
    SetVectorVis sv_vis(move_dat);

    sv_vis.SetMakeVector(true);
    m_rgn.AcceptWithNthObj(sv_vis, dat.curPos);

    sv_vis.SetMakeVector(false);
    m_rgn.Accept(sv_vis);
}

// изменить размеры выделенных объектов и получить набор областей для отрисовки
class ResizeVis: public MoveVis
{
    typedef MoveVis MyParent;
    public:
                  ResizeVis(int x, int y, Point& orig_pnt, const int_array& sel_arr)
                    : MyParent(x, y, orig_pnt, sel_arr) { }

   virtual  void  VisitImpl(MenuRegion& menu_rgn);
   virtual  void  Visit(FrameThemeObj& fto) { ResizeObj(fto, MakeFTOMoving(fto)); }
   virtual  void  Visit(TextObj& t_obj)     { ResizeObj(t_obj, MakeTextMoving(t_obj)); }

            void  ResizeObj(Comp::MediaObj& m_obj, Manager ming);
};

void ResizeVis::VisitImpl(MenuRegion& menu_rgn)
{
    CalcMoveVector();
    MyParent::VisitImpl(menu_rgn);
}

void ResizeVis::ResizeObj(Comp::MediaObj& m_obj, Manager ming)
{
    if( IsObjSelected() )
    {
        Draw(ming); 

        // новое положение
        Rect new_rct = m_obj.Placement(); 
        SizeVectorData& svd = m_obj.GetData<SizeVectorData>();
        svd.CalcPlacement(new_rct, lct);
        m_obj.SetPlacement(new_rct);

        ming->Update();

        Draw(ming); 
    }
}

void ResizeSelect::OnMouseMove(MEditorArea& edt_area, GdkEventMotion* event)
{
    MoveSelect::Data& dat = edt_area;
    ResizeVis rs_vis((int)event->x, (int)event->y, dat.origCoord, edt_area.SelArr());
    //SaveRectList(rs_vis.RectList());
    RenderByRLV(edt_area, rs_vis);
    UpdateToolbar(true);
}

//////////////////////////////////////////////////

namespace MEdt {

SelectData::SelectData(): selStt(0)
{
    // начальное состояние
    ChangeState(&NormalSelect::Instance());
}

void SelectData::ChangeState(SelectState* new_stt)
{
    selStt = new_stt;
}

} // namespace MEdt


