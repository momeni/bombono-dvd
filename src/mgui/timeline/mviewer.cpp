//
// mgui/timeline/mviewer.cpp
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

#include "mviewer.h"

#include <mgui/trackwindow.h>
#include <mgui/sdk/window.h>
#include <mgui/sdk/widget.h>
#include <mgui/sdk/packing.h>
#include <mgui/gettext.h>
#include <mgui/dialog.h>
#include <mgui/prefs.h>

#include <mlib/filesystem.h>
#include <mlib/sigc.h>

#include <gtk/gtkhpaned.h> // GTK_IS_HPANED()

//////////////////////////////////////////////////////////////////
// Временное подключение просмотра VOB из DVD-VIDEO в MViewer
//#define VOB_MVIEWER_INTRUSION

#ifdef VOB_MVIEWER_INTRUSION
#include <mdemux/dvdread.h>

struct Bunch: public Singleton<Bunch>
{
    dvd_reader_t* dvd;
    DVD::VobArr dvd_vobs; // массив всех VOB
    ptr::one<DtorAction> wrp;
};

#endif // VOB_MVIEWER_INTRUSION

//////////////////////////////////////////////////////////////////

template<class Cont1, class Cont2>
void CopyContainer(Cont1& cont1, Cont2& cont2)
{
    std::copy(cont1.begin(), cont1.end(), std::back_inserter(cont2));
}

Str::List GetFilenames(Gtk::FileChooser& fc)
{
    // я знаю, что это жутко неэффективно (2+1 копирования), но не актуально
    typedef Glib::SListHandle<Glib::ustring> UList;
    UList ulist = fc.get_filenames();

    Str::List list;
    CopyContainer(ulist, list);
    return list;
}

void OpenFile(Gtk::FileChooser& fc, OpenFileFnr fnr)
{
    try
    {
        Glib::ustring fname_ = fc.get_filename();
        const char*   fname  = fname_.c_str();

        if( fs::exists(fname) && fs::is_directory(fname) )
        {
            // просто откроем директорию, если выделена только она
            if( fc.get_filenames().size() == 1 )
            {
                fc.set_current_folder(fname);
                return;
            }
        }

        Str::List paths(GetFilenames(fc));
        fnr(fname, paths);
    }
    catch( const fs::filesystem_error& fe )
    {
        ErrorBox(_("Error while opening file:"), FormatFSError(fe));
    }
}

// ну что тут скажешь - HIG
/* Override the style properties with HIG-compliant spacings.  Ugh.
 * http://developer.gnome.org/projects/gup/hig/1.0/layout.html#layout-dialogs
 * http://developer.gnome.org/projects/gup/hig/1.0/windows.html#alert-spacing
 */

void MakeBoxHIGed(Gtk::VBox& vbox)
{
//     vbox.set_border_width(12);
//     vbox.set_spacing(24);
    vbox.set_border_width(0);
    vbox.set_spacing(WDG_BORDER_WDH);
}

static void MakeButtonAreaHIGed(Gtk::HButtonBox& action_area)
{
    action_area.set_border_width(0);
    action_area.set_spacing(WDG_BORDER_WDH);
}

static void ForAllWidgetsImpl(GtkWidget* wdg, gpointer data)
{
//     static int tabs = 0;
//     for( int i=0; i<tabs; i++ )
//         io::cout << "    ";
//     io::cout << g_type_name(G_TYPE_FROM_INSTANCE(wdt)) << io::endl;

    const GtkWidgetFunctor& fnr = *reinterpret_cast<const GtkWidgetFunctor*>(data);
    fnr(wdg);

    if( GTK_IS_CONTAINER(wdg) )
    {
        //tabs++;
        gtk_container_forall(GTK_CONTAINER(wdg), ForAllWidgetsImpl, data);
        //tabs--;
    }
}

void ForAllWidgets(GtkWidget* wdg, const GtkWidgetFunctor& fnr)
{
    ForAllWidgetsImpl(wdg, const_cast<gpointer>(static_cast<gconstpointer>(&fnr)));
}

static void HideBookMarks(GtkWidget* wdt)
{
    if( GTK_IS_HPANED(wdt) )
    {
        GtkPaned* paned = GTK_PANED(wdt);
        //gtk_paned_set_position(paned, 0);

        GtkWidget* ch = gtk_paned_get_child1(paned);
        //gtk_widget_set_size_request(ch, 0, 0);
        gtk_widget_hide(ch);
    }
}

Gtk::Container& PackAlignedForBrowserTB(Gtk::Container& par_contr, bool left_padding)
{
    return Add(par_contr, NewPaddingAlg(WDG_BORDER_WDH, WDG_BORDER_WDH, left_padding ? WDG_BORDER_WDH : 0, 0));
}

Gtk::HButtonBox& InsertButtonArea(Gtk::VBox& vbox, Gtk::ButtonBoxStyle style)
{
    // см. gtk_dialog_init()
    Gtk::HButtonBox& action_area = *Gtk::manage(new Gtk::HButtonBox);
    action_area.set_layout(style);
    //vbox.add(action_area);
    vbox.pack_start(action_area, false, true, 0);
    MakeButtonAreaHIGed(action_area);

    return action_area;
}

Gtk::Label& MakeTitleLabel(const char* name)
{
    // не меньше чем размер шрифта элемента в списке
    Gtk::Label& label = NewMarkupLabel("<span font_desc=\"Sans Bold 12\">" + 
                                       std::string(name) + "</span>");
    label.set_padding(0, 5);

    return label;
}

// значения соответ. строкам в PackFileChooserWidget()
enum FCWFilterType
{
    fftALL_FORMATS,
    fftVIDEO,
    fftDVD_SOUND,
    fftIMAGES,
    fftALL
};

static bool OnGlobMatch(const Gtk::FileFilter::Info& inf, const std::string& pat)
{
    return Fnmatch(inf.display_name, pat);
}

void AddGlobFilter(Gtk::FileFilter& ff, const std::string& pat)
{
    ff.add_custom(Gtk::FILE_FILTER_DISPLAY_NAME, 
                  wrap_return<bool>(bb::bind(&OnGlobMatch, _1, pat)));
}

// Список адаптирован из vlc_interface.h (плейер VLC)
// Видео здесь - все, что включает видеопоток; что из медиаформатов не включает
// видео (есть только аудио) - идет в список аудио (так принято де факто,- VLC, Mplayer, Totem, ...)
const char* video_extensions[] =
{
    "3g2","3gp","3gp2","3gpp","amv","asf","avi","bin","cue","divx","dv","flv","gxf","iso","m1v","m2v",
    "m2t","m2ts","m4v","mkv","mov","mp2","mp2v","mp4","mp4v","mpa","mpe","mpeg","mpeg1",
    "mpeg2","mpeg4","mpg","mpv2","mts","mxf","nsv","nuv",
    "ogg","ogm","ogv","ogx","ps",
    "rec","rm","rmvb","tod","ts","tts","vob","vro","webm","wmv"
};

static void AddVideoFilter(Gtk::FileFilter& ff)
{
#ifdef FFMPEG_IMPORT_POLICY
    boost_foreach( const char* ext, video_extensions )
        AddGlobFilter(ff, std::string("*.") + ext);
#else
    ff.add_pattern("*.m2v");
    ff.add_pattern("*.mpeg");
    ff.add_pattern("*.mpg");
    //ff.add_pattern("*.vob");
    //ff.add_pattern("*.VOB");
    AddGlobFilter(ff, "*.vob");
#endif
}

void FillSoundFilter(Gtk::FileFilter& ff)
{
    ff.add_pattern("*.mp2");
    ff.add_pattern("*.mpa");
    ff.add_pattern("*.ac3");
    ff.add_pattern("*.dts");
    ff.add_pattern("*.lpcm");
}

static void AddImagesFilter(Gtk::FileFilter& ff)
{
    ff.add_pattern("*.png");
    ff.add_pattern("*.jpg");
    ff.add_pattern("*.jpeg");
    ff.add_pattern("*.bmp");
}

static void AddAllFormatsFilter(Gtk::FileFilter& ff)
{
    AddVideoFilter(ff);
    FillSoundFilter(ff);
    AddImagesFilter(ff);
}

static void SetFilter(Gtk::FileChooserWidget& fcw, FCWFilterType typ)
{
    Gtk::FileFilter ff;
    //ff.set_name(combo.get_active_text());
    switch( typ )
    {
    case fftALL_FORMATS:
        AddAllFormatsFilter(ff);
        break;
    case fftVIDEO:
        AddVideoFilter(ff);
        break;
    case fftDVD_SOUND:
        FillSoundFilter(ff);
        break;
    case fftIMAGES:
        AddImagesFilter(ff);
        break;
    case fftALL:
        ff.add_pattern("*");
        break;
    default:
        ASSERT(0);
    }
    fcw.set_filter(ff);
}

static void OnChangeFCWFilter(Gtk::ComboBoxText& combo, Gtk::FileChooserWidget& fcw)
{
    FCWFilterType typ = (FCWFilterType)combo.get_active_row_number();
    SetFilter(fcw, typ);
}

std::string AudioForDVDTitle()
{
    return _("Audio for DVD") + std::string(" (*.mp2/mpa, *.ac3, *.dts, *.lpcm)");
}

std::string StillImagesTitle()
{
    return _("Still images") + std::string(" (*.png, *.jpg, *.jpeg, *.bmp)");
}

ActionFunctor PackFileChooserWidget(Gtk::Container& contr, OpenFileFnr fnr, bool is_mviewer)
{
    Gtk::VBox& vbox = *Gtk::manage(new Gtk::VBox);
    PackAlignedForBrowserTB(contr).add(vbox);
    MakeBoxHIGed(vbox);

    // 0 надпись
    vbox.pack_start(PackWidgetInFrame(MakeTitleLabel(_("File Browser")), Gtk::SHADOW_ETCHED_IN), Gtk::PACK_SHRINK);

    // 1 окно выбора файлов
    Gtk::FileChooserWidget& fcw = *Gtk::manage(new Gtk::FileChooserWidget(Gtk::FILE_CHOOSER_ACTION_OPEN));
    vbox.pack_start(fcw, true, true, 0);
    ActionFunctor open_fnr = bb::bind(&OpenFile, boost::ref(fcw), fnr);

    fcw.set_local_only(true);
    // для добавления множества файлов в проект
    fcw.set_select_multiple(true);
    fcw.signal_file_activated().connect(open_fnr);
    fcw.set_size_request(0, 0);
    ForAllWidgets(static_cast<Gtk::Widget&>(fcw).gobj(), HideBookMarks);

    if( is_mviewer )
    {
        // 2 кнопка загрузить 
        Gtk::HButtonBox& action_area = InsertButtonArea(vbox, Gtk::BUTTONBOX_END);

        Gtk::Button& load_btn = *Gtk::manage(new Gtk::Button(Gtk::Stock::OPEN));
        load_btn.signal_clicked().connect(open_fnr);
        action_area.pack_end(load_btn, false, true, 0);

        //load_btn.property_can_default() = true;
        //load_btn.grab_default();
        SetDefaultButton(load_btn);
    }
    else
    {
        Gtk::ComboBoxText& combo = *Gtk::manage(new Gtk::ComboBoxText);
        combo.append_text(_("All formats"));
#ifdef FFMPEG_IMPORT_POLICY
        combo.append_text(_("Video files"));
#else
        combo.append_text(_("MPEG files") + std::string(" (*.mpeg, *.mpg, *.vob)"));
#endif
        combo.append_text(AudioForDVDTitle());
        combo.append_text(StillImagesTitle());
        combo.append_text(_("All files (*.*)"));

        // значение по умолчанию
        combo.set_active(fftALL_FORMATS);
        SetFilter(fcw, fftALL_FORMATS);

        combo.signal_changed().connect( 
            bb::bind(&OnChangeFCWFilter, boost::ref(combo), boost::ref(fcw)) );
        vbox.pack_start(combo, Gtk::PACK_SHRINK);
    }

    return open_fnr;
}

FileFilter& AddFileFilter(FileFilterList& lst, const std::string& title)
{
    lst.push_back(title);
    return lst.back();
}

FileFilter& AddFPs(FileFilter& ff, const char** lst)
{
    for( ; *lst; lst++ )
        ff.extPatLst.push_back(*lst);
    return ff;
}

void AddAllFF(FileFilterList& lst)
{
    AddFileFilter(lst, _("All files (*.*)")).extPatLst.push_back("*");
}

void FillFPLforMedias(FileFilterList& lst)
{
    FileFilter& all_ff = AddFileFilter(lst, _("All formats"));

    FileFilter& vd_ff  = AddFileFilter(lst, _("Video files"));
#ifdef FFMPEG_IMPORT_POLICY
    boost_foreach( const char* ext, video_extensions )
        vd_ff.extPatLst.push_back(std::string("*.") + ext);
#else
#error "Not implemented"
#endif

    FileFilter& afd_ff = AddFileFilter(lst, AudioForDVDTitle());
    const char* afd_extensions[] = { "*.mp2", "*.mpa", "*.ac3", "*.dts", "*.lpcm", 0 };
    AddFPs(afd_ff, afd_extensions);

    FileFilter& pic_ff = AddFileFilter(lst, StillImagesTitle());
    const char* pic_extensions[] = { "*.png", "*.jpg", "*.jpeg", "*.bmp", 0 };
    AddFPs(pic_ff, pic_extensions);

    CopyContainer(vd_ff.extPatLst, all_ff.extPatLst);
    AddFPs(all_ff, afd_extensions);
    AddFPs(all_ff, pic_extensions);

    AddAllFF(lst);
}

static void OpenFileWithTrackLayout(TrackLayout& trk, const char* fnam)
{
    Gtk::Window& win = *GetTopWindow(trk);
    using namespace Project;
    VideoItem vd(new VideoMD);
    vd->MakeByPath(fnam);

    std::string err_str;
    if( OpenTrackLayout(trk, vd, err_str) )
    {
        win.set_title(Glib::filename_to_utf8(Glib::path_get_basename(fnam)));
        GrabFocus(trk);
    }
    else
    {
        win.set_title("MViewer");
        //const char* err_str = trk.GetMonitor().GetViewer().MInfo().ErrorReason();

        Gtk::MessageDialog mdlg(win, err_str, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
        mdlg.run();
    }
}

static void SetGarmonicHeightForTrk(Gtk::VPaned& vpaned)
{
    // выставляем высоту монтажного окна
//     int wdh, hgt;
//     win.get_default_size(wdh, hgt);
    int hgt = vpaned.get_height();
    int layout_hgt = 175; // вручную высчитал
    int vpos = hgt - layout_hgt;
    if( vpos < layout_hgt )
        vpos = hgt/2;
    vpaned.set_position(vpos);
}

void PackTrackWindow(Gtk::Container& contr, TWFunctor tw_fnr)
{
    Gtk::VPaned& vpaned = *Gtk::manage(new Gtk::VPaned);
    contr.add(vpaned);

    Timeline::DAMonitor& mon = *Gtk::manage(new Timeline::DAMonitor);

#ifdef VOB_MVIEWER_INTRUSION
    {
        Mpeg::Player& plyr = mon.GetPlayer();
        Bunch& bunch = Bunch::Instance();

        //const char* dvd_path = "/mnt/ntfs/DVD_Demystified";
        const char* dvd_path = "/media/cdrom";

        bunch.dvd = DVDOpen(dvd_path);
        bunch.wrp = new DtorAction(bl::bind(&DVDClose, bunch.dvd));

        DVD::FillVobArr(bunch.dvd_vobs, bunch.dvd);
        DVD::VobPtr vob = DVD::FindVob(bunch.dvd_vobs, 1, 1); //34, 1); 
        ptr::shared<DVD::VobStreambuf> strm_buf = new DVD::VobStreambuf(vob, bunch.dvd);

        io::cout << "Vob size = " << strm_buf->Size() << io::endl;
        
        plyr.OpenFBuf(strm_buf);
    }
#endif // VOB_MVIEWER_INTRUSION

    TrackLayout& layout = *Gtk::manage(new TrackLayout(mon));
    {
        Gtk::VBox& vbox = *Gtk::manage(new Gtk::VBox);
        vpaned.add1(vbox);

        Gtk::HPaned& hpaned = *Gtk::manage(new Gtk::HPaned);
        SetUpdatePos(hpaned, UnnamedPrefs().srcBrw1Wdh);
        vbox.pack_start(hpaned, true, true, 0);

        tw_fnr(hpaned, mon, layout);

        Gtk::HSeparator& hsep = *Gtk::manage(new Gtk::HSeparator);
        vbox.pack_end(hsep, false, true, 0);
    }

    // 2 шкала
    PackTrackLayout(vpaned, layout);

    // 3 приведение к гармоничной форме
    gtk_container_child_set(vpaned.Gtk::Container::gobj(), 
                            vpaned.get_child1()->gobj(), "resize", TRUE,  NULL);
    gtk_container_child_set(vpaned.Gtk::Container::gobj(), 
                            vpaned.get_child2()->gobj(), "resize", FALSE, NULL);

    vpaned.signal_realize().connect(bb::bind(&SetGarmonicHeightForTrk, boost::ref(vpaned)));
}

void PackMonitor(Gtk::HPaned& hpaned, Timeline::DAMonitor& mon, TrackLayout& layout)
{
    // 1 окно выбора файлов
    PackFileChooserWidget(hpaned, bb::bind(&OpenFileWithTrackLayout, boost::ref(layout), _1),
                          true);

    // 1.1 монитор
    hpaned.add2(PackMonitorIn(mon));
}

void RunMViewer()
{
    Gtk::Window win;
    win.set_title("MViewer");

    win.set_default_size(800, 600);
    PackTrackWindow(win, bb::bind(&PackMonitor, _1, _2, _3));

    RunWindow(win);
}


