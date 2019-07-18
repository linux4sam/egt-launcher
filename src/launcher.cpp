/*
 * Copyright (C) 2018 Microchip Technology Inc.  All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <algorithm>
#include <array>
#include <egt/detail/filesystem.h>
#include <egt/detail/imagecache.h>
#include <egt/ui>
#include <experimental/filesystem>
#include <iostream>
#include <memory>
#include <rapidxml.hpp>
#include <rapidxml_utils.hpp>
#include <regex>
#include <string>
#include <vector>

#ifdef HAVE_EGT_DETAIL_SCREEN_KMSSCREEN_H
#include <egt/detail/screen/kmsscreen.h>
#endif

using namespace std;
using namespace egt;

namespace filesys = std::experimental::filesystem;

#ifdef DO_SCALING
/**
 * Calculate a scale relative to how far something is the center.
 */
static float sliding_scale(int win_w, int item_w, int item_pos,
                           float min = 0.5, float max = 1.0)
{
    float range = win_w / 2;
    float delta = std::fabs(range - (item_pos + (item_w / 2)));
    float scale = 1.0 - (delta / range);
    if (scale < min || scale > max)
        return min;
    return scale;
}
#endif

/**
 * Execute a command.
 */
static std::string exec(const char* cmd, bool wait = false)
{
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe)
        throw std::runtime_error("popen() failed!");
    if (wait)
    {
        while (!feof(pipe.get()))
        {
            if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
                result += buffer.data();
        }
    }

    return result;
}

#define ITEM_SPACE 250

static int itemnum = 0;

/*
 * A launcher menu item.
 */
class LauncherItem : public ImageLabel
{
public:
    LauncherItem(const string& name, const string& description,
                 const string& image, const string& exec, int x = 0, int y = 0)
        : ImageLabel(Image(image),
                     name,
                     Rect(Point(x, y), Size()),
                     alignmask::center),
          m_num(itemnum++),
          m_name(name),
          m_description(description),
          m_exec(exec)
    {
        set_color(Palette::ColorId::label_text, Palette::white);
        set_image_align(alignmask::center | alignmask::top);
        set_text_align(alignmask::center | alignmask::bottom);
    }

    void handle(Event& event) override
    {
        ImageLabel::handle(event);

        switch (event.id())
        {
        case eventid::pointer_click:
        {
            Application::instance().event().quit();

#ifdef HAVE_EGT_DETAIL_SCREEN_KMSSCREEN_H
            // explicitly close KMS
            if (detail::KMSScreen::instance())
                detail::KMSScreen::instance()->close();
#endif

            string cmd = DATADIR "/egt/launcher/launch.sh " + m_exec + " &";
            exec(cmd.c_str());

            event.stop();
            break;
        }
        default:
            break;
        }
    }

    void scale_box(int pos)
    {
        ignoreparam(pos);

#ifdef DO_SCALING
        auto c = center();
        float s = sliding_scale(parent()->w(), w(), pos);
        label_enabled(s > 0.9);
        scale_image(s, true);
        move_to_center(c);
#endif
    }

    inline int num() const { return m_num; }

private:
    int m_num;
    string m_name;
    string m_description;
    string m_exec;
};

/**
 * Main launcher window.
 */
class LauncherWindow : public TopWindow
{
public:
    LauncherWindow()
        : m_animation(0, 0, std::chrono::milliseconds(200),
                      easing_quintic_easein)
    {
        m_animation.on_change(std::bind(&LauncherWindow::move_boxes, this,
                                        std::placeholders::_1));

        add(make_shared<ImageLabel>(Image("background.png")));

        auto logo = std::make_shared<ImageLabel>(Image("@128px/microchip_logo_white.png"));
        logo->set_align(alignmask::center | alignmask::bottom);
        logo->set_margin(10);
        add(logo);

        auto egt_logo = std::make_shared<ImageLabel>(Image("@128px/egt_logo_white.png"));
        egt_logo->set_align(alignmask::center | alignmask::top);
        egt_logo->set_margin(10);
        add(egt_logo);
    }

    std::vector<std::string> get_files(const std::string &dir)
    {
        std::vector<std::string> files;

        try
        {
            if (filesys::exists(dir) && filesys::is_directory(dir))
            {
                filesys::recursive_directory_iterator iter(dir);
                filesys::recursive_directory_iterator end;

                while (iter != end)
                {
                    if (!filesys::is_directory(iter->path().string()))
                    {
                        std::regex rx(".*\\.xml$");
                        if (std::regex_match(iter->path().string(), rx))
                            files.push_back(iter->path().string());
                    }

                    error_code ec;
                    iter.increment(ec);
                    if (ec)
                        std::cerr << "Error While Accessing : " << iter->path().string() << " :: " << ec.message() << '\n';
                }
            }
        }
        catch (std::system_error & e)
        {
            std::cerr << "Exception :: " << e.what();
        }

        // give some determinism to the order of results
        std::sort(files.begin(), files.end());

        return files;
    }

    virtual void load_entry(rapidxml::xml_node<>* node)
    {
        if (!node->first_node("title"))
            return;

        string name = node->first_node("title")->value();

        string description;

        if (node->first_node("description"))
            description = node->first_node("description")->value();

        string image;
        auto link = node->first_node("link");
        if (link)
        {
            auto href = link->first_attribute("href");
            if (href)
                image = href->value();
        }

        if (!node->first_node("arg"))
            return;

        string cmd = node->first_node("arg")->value();

        auto box = make_shared<LauncherItem>(name, description, image, cmd);
        add(box);
        box->resize(Size(box->width(), height() / 2));
        box->move_to_center(Point(m_boxes.size() * ITEM_SPACE, height() / 2));

#ifdef DO_SCALING
        // pre-seed the image cache
        for (auto s = 0.5; s <= 2.0; s += 0.01)
            box->scale_box(s);
#endif

        box->scale_box(m_boxes.size() * ITEM_SPACE - box->width() / 2);

        m_boxes.push_back(box);
    }

    virtual int load(const std::string& dir)
    {
        std::vector<std::string> files = get_files(dir);

        for (auto& file : files)
        {
            rapidxml::file<> xml_file(file.c_str());
            rapidxml::xml_document<> doc;
            doc.parse<0>(xml_file.data());

            auto feed = doc.first_node("feed");
            if (feed)
            {
                for (auto screen = feed->first_node("screen"); screen; screen = screen->next_sibling())
                {
                    for (auto entry = screen->first_node("entry"); entry; entry = entry->next_sibling())
                    {
                        detail::add_search_path(detail::extract_dirname(file));
                        load_entry(entry);
                    }
                }
            }
            else
            {
                for (auto entry = doc.first_node("entry"); entry; entry = entry->next_sibling())
                {
                    detail::add_search_path(detail::extract_dirname(file));
                    load_entry(entry);
                }
            }
        }

        start_snap();

        return 0;
    }

    void handle(Event& event) override
    {
        TopWindow::handle(event);

        switch (event.id())
        {
        case eventid::raw_pointer_down:
            if (!m_moving)
            {
                m_moving = true;
                m_moving_x = event.pointer().point.x();
                m_offset = m_boxes[0]->center().x();
            }
            break;
        case eventid::raw_pointer_up:
            m_moving = false;
            start_snap();
            break;
        case eventid::raw_pointer_move:
            if (m_moving)
            {
                move_boxes(event.pointer().point.x());
                event.stop();
            }
            break;
        default:
            break;
        }
    }

    void move_boxes(int x)
    {
        auto diff = x - m_moving_x;

        for (auto& box : m_boxes)
        {
            auto pos = m_offset + (box->num() * ITEM_SPACE) + diff;

            Rect to(box->box());
            to.set_x(pos);

            bool visible = Rect::intersect(Rect::merge(to, box->box()), this->box());
            if (visible)
            {
                box->move_to_center(Point(pos, box->center().y()));
                box->scale_box(pos - box->width() / 2);
            }
            else
            {
                box->move_to_center(Point(pos, box->center().y()));
            }
        }
    }

    void start_snap()
    {
        if (m_boxes.empty())
            return;

        m_animation.stop();

        auto center = box().center();
        auto distance = width();

        for (auto& box : m_boxes)
        {
            if (center.distance_to(box->box().center()) < std::abs(distance))
            {
                distance = center.distance_to(box->box().center());
                if (center.x() < box->box().center().x())
                    distance *= -1;
            }
        }

        m_animation.set_starting(0);
        m_animation.set_ending(distance);
        m_animation.set_duration(std::chrono::milliseconds(static_cast<uint32_t>(std::abs(distance))));
        m_animation.start();

        m_moving_x = 0;
        m_offset = m_boxes[0]->center().x();
    }

private:
    bool m_moving {false};
    int m_moving_x{0};
    int m_offset{0};
    vector<shared_ptr<LauncherItem>> m_boxes;
    PropertyAnimator m_animation;
};

int main(int argc, const char** argv)
{
    Application app(argc, argv);

    detail::add_search_path(DATADIR "/egt/launcher/");

    LauncherWindow win;

    // load some default directories if nothing is specified
    if (argc <= 1)
    {
        win.load(DATADIR "/egt/examples/");
        win.load(DATADIR "/egt/samples/");
        win.load("/opt/ApplicationLauncher/applications/xml/");
    }
    else
    {
        for (auto i = 1; i < argc; i++)
            win.load(argv[i]);
    }

    win.show();

    return app.run();
}
