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
#include <egt/ui>
#include <experimental/filesystem>
#include <fstream>
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

class LauncherWindow;

/**
 * Normalize a value to a range.
 *
 * This assumes the value wraps when it goes below @b start or above @b end, and
 * returns a value within the allowed range.
 */
template<class T>
T normalize_to_range(const T value, const T start, const T end)
{
    const auto width = end - start;
    const auto offset = value - start;

    return (offset - ((offset / width) * width)) + start;
}

template<>
double normalize_to_range<double>(const double value, const double start, const double end)
{
    const auto width = end - start;
    const auto offset = value - start;

    return (offset - (floor(offset / width) * width)) + start ;
}

template<>
float normalize_to_range<float>(const float value, const float start, const float end)
{
    const auto width = end - start;
    const auto offset = value - start;
    return (offset - (floor(offset / width) * width)) + start ;
}

/*
 * A launcher menu item.
 */
class LauncherItem : public ImageLabel
{
    static int itemnum;

public:

    LauncherItem(LauncherWindow& window,
                 const string& name, const string& description,
                 const string& image, const string& exec, int x = 0, int y = 0)
        : ImageLabel(Image(image),
                     name,
                     Rect(Point(x, y), Size()),
                     AlignFlag::center),
          m_window(window),
          m_num(itemnum++),
          m_name(name),
          m_description(description),
          m_exec(exec)
    {
        flags().set(Widget::Flag::no_layout);
        color(Palette::ColorId::label_text, Palette::white);
        image_align(AlignFlag::center | AlignFlag::top);
        text_align(AlignFlag::center | AlignFlag::bottom);
    }

    virtual void handle(Event& event) override;

    inline int num() const { return m_num; }
    inline string name() const { return m_name; }
    inline double angle() const { return m_angle; }
    inline void angle(double angle)
    {
        m_angle = normalize_to_range<double>(angle, 0, 360);
#ifdef ANGLE_DEBUG
        ostringstream ss;
        ss << m_angle;
        text(ss.str());
#endif
    }

private:
    LauncherWindow& m_window;
    int m_num{0};
    double m_angle{0.};
    string m_name;
    string m_description;
    string m_exec;
};

int LauncherItem::itemnum = 0;
const auto OFFSET_FILENAME = "/tmp/egt-launcher-offset";

/**
 * Main launcher window.
 */
class LauncherWindow : public TopWindow
{
public:
    LauncherWindow()
    {
        background(Image("file:background.png"));

        auto logo = std::make_shared<ImageLabel>(Image("icon:microchip_logo_white.png;128"));
        logo->align(AlignFlag::center | AlignFlag::bottom);
        logo->margin(10);
        add(logo);

        auto egt_logo = std::make_shared<ImageLabel>(Image("icon:egt_logo_white.png;128"));
        egt_logo->align(AlignFlag::center | AlignFlag::top);
        egt_logo->margin(10);
        add(egt_logo);
    }

    void launch(const std::string& exe) const
    {
        Application::instance().event().quit();

#ifdef HAVE_EGT_DETAIL_SCREEN_KMSSCREEN_H
        // explicitly close KMS
        if (detail::KMSScreen::instance())
            detail::KMSScreen::instance()->close();
#endif

        save_offset();

        string cmd = DATADIR "/egt/launcher/launch.sh " + exe + " &";
        exec(cmd.c_str());
    }

    static std::vector<std::string> get_files(const std::string& dir)
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
                    {
                        std::cerr << "error accessing: " <<
                                  iter->path().string() << " :: " << ec.message() << endl;
                    }
                }
            }
        }
        catch (std::system_error& e)
        {
            std::cerr << "exception: " << e.what() << endl;
        }

        // give some determinism to the order of results
        std::sort(files.begin(), files.end());

        return files;
    }

    void load_entry(rapidxml::xml_node<>* node)
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

        auto box = make_shared<LauncherItem>(*this, name, description, "file:" + image, cmd);
        box->resize(Size(box->width(), height() / 2));
        m_boxes.push_back(box);
        add(box);
    }

    int load(const std::string& dir)
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
                        add_search_path(detail::extract_dirname(file));
                        load_entry(entry);
                    }
                }
            }
            else
            {
                for (auto entry = doc.first_node("entry"); entry; entry = entry->next_sibling())
                {
                    add_search_path(detail::extract_dirname(file));
                    load_entry(entry);
                }
            }
        }

        m_drag_angles.clear();

        auto a =  width() * 1.25f / 2.f;
        auto b =  height() / 2.f;

        const auto min_perimeter = 225 * m_boxes.size();
        while (true)
        {
            m_ellipse.radiusa(a);
            m_ellipse.radiusb(b);

            if (m_ellipse.perimeter() >= min_perimeter)
                break;

            a *= 1.01;
            b *= 1.02;
        }

        m_ellipse.center(PointType<float>(width() / 2,
                                              height() / 2 - m_ellipse.radiusb()));

        // evenly space each item at an angle
        auto anglesep = 360. / m_boxes.size();
        auto angleoffset = load_offset();
        for (auto& box : m_boxes)
        {
            box->angle(angleoffset + (box->num() * anglesep));
            m_drag_angles.push_back(box->angle());
        }

        move_boxes();

        return 0;
    }

    double load_offset() const
    {
        double offset = 90.;
        ifstream in(OFFSET_FILENAME);
        if (in.is_open())
            in >> offset;
        return offset;
    }

    void save_offset() const
    {
        if (m_boxes.empty())
            return;

        auto offset = m_boxes.front()->angle();
        ofstream out(OFFSET_FILENAME, ios::trunc);
        if (out.is_open())
            out << offset;
    }

    void lines(istream& in)
    {
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty())
                m_lines.push_back(line);
        }

        if (!m_lines.empty())
        {
            auto vsizer = std::make_shared<Frame>(Size(width(), height() * .3f));
            vsizer->move(Point(0, height() - height() * .3f));
            add(vsizer);

            auto label = std::make_shared<Label>();
            label->color(Palette::ColorId::label_text, Palette::white);
            vsizer->add(expand(label));

            auto minx = 0 - vsizer->width();
            auto maxx = width();
            auto half = (width() - vsizer->width()) / 2;

            auto in = std::make_shared<PropertyAnimator>(maxx, half,
                      std::chrono::seconds(3),
                      easing_exponential_easeout);
            in->on_change([vsizer](int value)
            {
                vsizer->x(value);
            });

            auto delay1 = std::make_shared<AnimationDelay>(std::chrono::seconds(2));

            auto out = std::make_shared<PropertyAnimator>(half + 1, minx,
                       std::chrono::seconds(3),
                       easing_exponential_easeout);
            out->reverse(true);
            out->on_change([this, vsizer, out, label](int value)
            {
                vsizer->x(value);

                static size_t index = 0;
                if (value == out->ending())
                {
                    label->text(m_lines[index]);
                    if (++index >= m_lines.size())
                        index = 0;
                }
            });

            auto delay2 = std::make_shared<AnimationDelay>(std::chrono::seconds(2));

            m_sequence.add(in);
            m_sequence.add(delay1);
            m_sequence.add(out);
            m_sequence.add(delay2);
            m_sequence.start();
        }
    }

    void handle(Event& event) override
    {
        TopWindow::handle(event);

        switch (event.id())
        {
        case EventId::pointer_drag_start:
            m_drag_angles.clear();
            for (auto& box : m_boxes)
                m_drag_angles.push_back(box->angle());
            break;
        case EventId::pointer_drag:
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

        auto angles = m_drag_angles.begin();
        for (auto& box : m_boxes)
        {
            const auto ANGLE_SPEED_FACTOR = width() * .0002;

            // adjust the box angle
            auto angle = *angles;
            angle -= (diff * ANGLE_SPEED_FACTOR);
            box->angle(angle);

            // x,y on the ellipse at the specified angle
            auto point = m_ellipse.point_on_perimeter(angle);

            box->move_to_center(Point(point.x(), point.y()));

            ++angles;
        }
    }

private:
    vector<shared_ptr<LauncherItem>> m_boxes;
    vector<double> m_drag_angles;
    EllipseType<float> m_ellipse{};
    vector<string> m_lines;
    PropertyAnimator m_animation;
    AnimationSequence m_sequence{true};
};

void LauncherItem::handle(Event& event)
{
    ImageLabel::handle(event);

    switch (event.id())
    {
    case EventId::pointer_click:
    {
        m_window.launch(m_exec);
        event.stop();
        break;
    }
    default:
        break;
    }
}

int main(int argc, char** argv)
{
    Application app(argc, argv);

    add_search_path(DATADIR "/egt/launcher/");
    add_search_path("images/");

    LauncherWindow win;

    // load some default directories if nothing is specified
    if (argc <= 1)
    {
        win.load(DATADIR "/egt/");
    }
    else
    {
        for (auto i = 1; i < argc; i++)
            win.load(argv[i]);
    }

    {
        ifstream in(egt::resolve_file_path("taglines.txt"), std::ios::binary);
        if (in.is_open())
            win.lines(in);
    }

    win.show();

    return app.run();
}
