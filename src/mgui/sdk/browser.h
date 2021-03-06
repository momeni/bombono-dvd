//
// mgui/sdk/browser.h
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

#ifndef __MGUI_SDK_BROWSER_H__
#define __MGUI_SDK_BROWSER_H__

//#include "player_utils.h"

#include <mgui/project/thumbnail.h>
#include <mgui/ffviewer.h>

RefPtr<Gdk::Pixbuf> GetThumbnail(const char* fpath, int thumb_wdh = THUMB_WDH);

// показывает адаптированный диалог "Open"
bool OpenMediaFile(std::string& fname, Gtk::Window& par_win);

namespace Project
{

typedef std::pair<VideoItem, double> VideoStart;
VideoStart GetVideoStart(MediaItem mi);

typedef boost::function<RefPtr<Gdk::Pixbuf>()> PixbufGetterFunctor;

// :TODO: упростить - вместо PixbufGetterFunctor просто передавать результат,
// соответ. VideoPE вырождается в ImagePE(read_only=true) (только ImagePE ноль не принимает)
// чтение кадров с видео
class VideoPE: public PixExtractor
{
    public:
                          VideoPE(const PixbufGetterFunctor& pgf): pgFnr(pgf) {}
                          VideoPE(VideoStart vs);
                          VideoPE(VideoViewer& plyr_, double time_);

    virtual PixbufSource  Make(const Point& sz);
    virtual         void  Fill(RefPtr<Gdk::Pixbuf>& pix);

    protected:
        PixbufGetterFunctor pgFnr;
};

} // namespace Project

#endif // #ifndef __MGUI_SDK_BROWSER_H__

