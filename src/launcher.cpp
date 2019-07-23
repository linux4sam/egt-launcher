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

/*
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
        flags().set(Widget::flag::no_layout);
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

    inline int num() const { return m_num; }
    inline string name() const { return m_name; }
    inline double angle() const { return m_angle; }
    inline void set_angle(double angle) { m_angle = angle; }

private:
    int m_num{0};
    double m_angle{0.};
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
    {
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

    std::vector<std::string> get_files(const std::string& dir)
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
        catch (std::system_error& e)
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
        box->resize(Size(box->width(), height() / 2));
        m_boxes.push_back(box);
        add(box);
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

        m_drag_angles.clear();

        // evenly space each item at an angle
        auto anglesep = 360. / m_boxes.size();
        for (auto& box : detail::reverse_iterate(m_boxes))
        {
            box->set_angle((box->num() * anglesep));
            m_drag_angles.push_back(box->angle());
        }

        move_boxes();

        return 0;
    }

    void handle(Event& event) override
    {
        TopWindow::handle(event);

        switch (event.id())
        {
        case eventid::pointer_drag_start:
            m_drag_angles.clear();
            for (auto& box : m_boxes)
                m_drag_angles.push_back(box->angle());
            break;
        case eventid::pointer_drag:
            move_boxes(event.pointer().point.x() - event.pointer().drag_start.x());
            event.stop();
            break;
        default:
            break;
        }
    }

    void move_boxes(int diff = 0)
    {
        if (m_boxes.empty() || m_boxes.size() != m_drag_angles.size())
            return;

        Point ecenter(center().x(), -400);
        auto a = 1000. / 2.;
        auto b = 1200. / 2.;

        auto angles = m_drag_angles.begin();
        for (auto& box : m_boxes)
        {
            // adjust the box angle
            auto angle = *angles;
            angle -= (diff * .2);
            box->set_angle(angle);

            // x,y on the ellipse at the specified angle with ellipse center at 0,0
            auto x = a * std::cos(detail::to_radians(90., angle));
            auto y = b * std::sin(detail::to_radians(90., angle));

            // adjust position of ellipse
            x += ecenter.x();
            y += ecenter.y();

            box->move_to_center(Point(x, y));

            ++angles;
        }
    }

private:
    vector<shared_ptr<LauncherItem>> m_boxes;
    vector<double> m_drag_angles;
};

int main(int argc, const char** argv)
{
    Application app(argc, argv);

    detail::add_search_path(DATADIR "/egt/launcher/");
    detail::add_search_path("images/");

    LauncherWindow win;

    // load some default directories if nothing is specified
    if (argc <= 1)
    {
        win.load(DATADIR "/egt/examples/");
        win.load(DATADIR "/egt/samples/");
        //win.load("/opt/ApplicationLauncher/applications/xml/");
    }
    else
    {
        for (auto i = 1; i < argc; i++)
            win.load(argv[i]);
    }

    win.show();

    return app.run();
}
