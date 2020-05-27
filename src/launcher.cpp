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
#include <cmath>
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

/**
 * Basic swipe detector which will invoke a callback with up/down/left/right.
 */
class SwipeDetect
{
public:

    using SwipeCallback = std::function<void(const std::string& direction)>;

    SwipeDetect() = delete;

    explicit SwipeDetect(SwipeCallback callback)
        : m_callback(std::move(callback))
    {
    }

    void handle(egt::Event& event)
    {
        switch (event.id())
        {
        case egt::EventId::raw_pointer_down:
        {
            m_start = event.pointer().point;
            m_start_time = std::chrono::steady_clock::now();
            break;
        }
        case egt::EventId::raw_pointer_up:
        {
            const auto elapsed = std::chrono::steady_clock::now() - m_start_time;
            if (elapsed <= m_allowed_time)
            {
                const auto dist = m_start - event.pointer().point;

                if (std::abs(dist.x()) >= m_threshold && std::abs(dist.y()) <= m_restraint)
                    m_callback((dist.x() < 0) ? "left" : "right");
                else if (std::abs(dist.y()) >= m_threshold && std::abs(dist.x()) <= m_restraint)
                    m_callback((dist.y() < 0) ? "up" : "down");
            };

            break;
        }
        default:
            break;
        }
    }

    void threshold(int value)
    {
        m_threshold = value;
    }

    int threshold() const
    {
        return m_threshold;
    }

    void restraint(int value)
    {
        m_restraint = value;
    }

    int restraint() const
    {
        return m_restraint;
    }

    void allowed_time(const std::chrono::milliseconds& value)
    {
        m_allowed_time = value;
    }

    std::chrono::milliseconds allowed_time() const
    {
        return m_allowed_time;
    }

protected:

    /// Required min distance traveled.
    int m_threshold{150};
    /// Maximum distance allowed at the same time.
    int m_restraint{100};
    /// Maximum time allowed to travel.
    std::chrono::milliseconds m_allowed_time{300};

private:

    /// Starting point
    egt::DisplayPoint m_start;
    /// Start time of m_start
    std::chrono::time_point<std::chrono::steady_clock> m_start_time;
    /// Callback to invoke when finished.
    SwipeCallback m_callback;
};

namespace filesys = std::experimental::filesystem;

/*
 * Execute a command.
 */
static std::string exec(const char* cmd, bool wait = false)
{
    std::string result;
    // NOLINTNEXTLINE(cert-env33-c)
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe)
        throw std::runtime_error("popen() failed!");
    if (wait)
    {
        while (!feof(pipe.get()))
        {
            std::array<char, 128> buffer{};
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

    return (offset - (std::floor(offset / width) * width)) + start ;
}

template<>
float normalize_to_range<float>(const float value, const float start, const float end)
{
    const auto width = end - start;
    const auto offset = value - start;
    return (offset - (std::floor(offset / width) * width)) + start ;
}

/*
 * A launcher menu item.
 */
class LauncherItem : public egt::ImageLabel
{
    static int itemnum;

public:

    LauncherItem(LauncherWindow& window,
                 // NOLINTNEXTLINE(modernize-pass-by-value)
                 const std::string& name, const std::string& description,
                 // NOLINTNEXTLINE(modernize-pass-by-value)
                 const std::string& image, const std::string& exec, int x = 0, int y = 0)
        : ImageLabel(egt::Image(image),
                     name,
                     egt::Rect(egt::Point(x, y), egt::Size()),
                     egt::AlignFlag::center),
          m_window(window),
          m_num(itemnum++),
          m_name(name),
          m_description(description),
          m_exec(exec)
    {
        flags().set(egt::Widget::Flag::no_layout);
        color(egt::Palette::ColorId::label_text, egt::Palette::white);
        image_align(egt::AlignFlag::center | egt::AlignFlag::bottom);
        text_align(egt::AlignFlag::center | egt::AlignFlag::top);
        font(egt::Font(20, egt::Font::Weight::bold));
    }

    void handle(egt::Event& event) override;

    inline int num() const { return m_num; }
    inline std::string name() const { return m_name; }
    inline double angle() const { return m_angle; }
    inline void angle(double angle)
    {
        m_angle = normalize_to_range<double>(angle, 0, 360);
#ifdef ANGLE_DEBUG
        std::ostringstream ss;
        ss << m_angle;
        text(ss.str());
#endif
    }

private:
    LauncherWindow& m_window;
    int m_num{0};
    double m_angle{0.};
    std::string m_name;
    std::string m_description;
    std::string m_exec;
};

int LauncherItem::itemnum = 0;
const auto OFFSET_FILENAME = "/tmp/egt-launcher-offset";

/**
 * Main launcher window.
 */
class LauncherWindow : public egt::TopWindow
{
public:
    LauncherWindow()
    {
        background(egt::Image("file:background.png"));

        auto logo = std::make_shared<egt::ImageLabel>(egt::Image("icon:microchip_logo_white.png;128"));
        logo->align(egt::AlignFlag::center | egt::AlignFlag::bottom);
        logo->margin(10);
        add(logo);

        auto egt_logo = std::make_shared<egt::ImageLabel>(egt::Image("icon:egt_logo_white.png;128"));
        egt_logo->align(egt::AlignFlag::center | egt::AlignFlag::top);
        egt_logo->margin(10);
        add(egt_logo);

        m_swipe_animation.on_change([this](egt::PropertyAnimator::Value value)
                                    {
                                        move_boxes(value);
                                    });
    }

    void launch(const std::string& exe) const
    {
        egt::Application::instance().event().quit();

#ifdef HAVE_EGT_DETAIL_SCREEN_KMSSCREEN_H
        // explicitly close KMS
        if (egt::detail::KMSScreen::instance())
            egt::detail::KMSScreen::instance()->close();
#endif

        save_offset();

        const std::string cmd = DATADIR "/egt/launcher/launch.sh " + exe + " &";
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

                    std::error_code ec;
                    iter.increment(ec);
                    if (ec)
                    {
                        std::cerr << "error accessing: " <<
                            iter->path().string() << " :: " << ec.message() << std::endl;
                    }
                }
            }
        }
        catch (std::system_error& e)
        {
            std::cerr << "exception: " << e.what() << std::endl;
        }

        // give some determinism to the order of results
        std::sort(files.begin(), files.end());

        return files;
    }

    void load_entry(rapidxml::xml_node<>* node)
    {
        if (!node->first_node("title"))
            return;

        const std::string name = node->first_node("title")->value();

        std::string description;
        if (node->first_node("description"))
            description = node->first_node("description")->value();

        std::string image;
        auto link = node->first_node("link");
        if (link)
        {
            auto href = link->first_attribute("href");
            if (href)
                image = href->value();
        }

        if (!node->first_node("arg"))
            return;

        std::string cmd = node->first_node("arg")->value();

        auto box = std::make_shared<LauncherItem>(*this, name, description, "file:" + image, cmd);
        box->resize(egt::Size(box->width(), height() / 2));
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
                for (auto screen = feed->first_node("screen"); screen; screen = screen->next_sibling("screen"))
                {
                    for (auto entry = screen->first_node("entry"); entry; entry = entry->next_sibling("entry"))
                    {
                        egt::add_search_path(egt::detail::extract_dirname(file));
                        load_entry(entry);
                    }
                }
            }
            else
            {
                for (auto entry = doc.first_node("entry"); entry; entry = entry->next_sibling("entry"))
                {
                    egt::add_search_path(egt::detail::extract_dirname(file));
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

        m_ellipse.center(egt::PointType<float>(width() / 2.0,
                                              height() / 2.0 - m_ellipse.radiusb()));

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

    static double load_offset()
    {
        double offset = 90.;
        std::ifstream in(OFFSET_FILENAME);
        if (in.is_open())
            in >> offset;
        return offset;
    }

    void save_offset() const
    {
        if (m_boxes.empty())
            return;

        auto offset = m_boxes.front()->angle();
        std::ofstream out(OFFSET_FILENAME, std::ios::trunc);
        if (out.is_open())
            out << offset;
    }

    void lines(std::istream& in)
    {
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty())
                m_lines.push_back(line);
        }

        if (!m_lines.empty())
        {
            auto vsizer = std::make_shared<egt::Frame>(egt::Size(width(), height() * .3f));
            vsizer->move(egt::Point(0, height() - height() * .3f));
            add(vsizer);

            auto label = std::make_shared<egt::Label>();
            label->color(egt::Palette::ColorId::label_text, egt::Palette::white);
            vsizer->add(egt::expand(label));

            auto minx = 0 - vsizer->width();
            auto maxx = width();
            auto half = (width() - vsizer->width()) / 2;

            auto in = std::make_shared<egt::PropertyAnimator>(maxx, half,
                      std::chrono::seconds(3),
                                                              egt::easing_exponential_easeout);
            in->on_change([vsizer](int value)
            {
                vsizer->x(value);
            });

            auto delay1 = std::make_shared<egt::AnimationDelay>(std::chrono::seconds(2));

            auto out = std::make_shared<egt::PropertyAnimator>(half + 1, minx,
                       std::chrono::seconds(3),
                                                               egt::easing_exponential_easeout);
            out->reverse(true);
            out->on_change([this, vsizer, out, label](int value)
            {
                vsizer->x(value);

                static size_t index = 0;
                if (egt::detail::float_equal(value, out->ending()))
                {
                    label->text(m_lines[index]);
                    if (++index >= m_lines.size())
                        index = 0;
                }
            });

            auto delay2 = std::make_shared<egt::AnimationDelay>(std::chrono::seconds(2));

            m_sequence.add(in);
            m_sequence.add(delay1);
            m_sequence.add(out);
            m_sequence.add(delay2);
            m_sequence.start();
        }
    }

    void handle(egt::Event& event) override
    {
        TopWindow::handle(event);

        switch (event.id())
        {
        case egt::EventId::pointer_drag_start:
            m_swipe_animation.stop();
            reset_angles();
            break;
        case egt::EventId::pointer_drag:
            {
                const auto dist = event.pointer().point - event.pointer().drag_start;
                move_boxes(dist.x());
                event.stop();
                break;
            }
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
            auto point = m_ellipse.point_on_circumference(egt::detail::to_radians<double>(0,angle));

            box->move_to_center(egt::Point(point.x(), point.y()));

            ++angles;
        }
    }

    void move_boxes_swipe(bool right)
    {
        // if animating, ignore event and wait for it to finish
        if (m_swipe_animation.running())
            return;

        reset_angles();

        if (right)
        {
            m_swipe_animation.starting(0);
            m_swipe_animation.ending(-200);
        }
        else
        {
            m_swipe_animation.starting(0);
            m_swipe_animation.ending(200);
        }

        m_swipe_animation.start();
    }

private:

    void reset_angles()
    {
        m_drag_angles.clear();
        for (auto& box : m_boxes)
            m_drag_angles.push_back(box->angle());
    }

    std::vector<std::shared_ptr<LauncherItem>> m_boxes;
    std::vector<double> m_drag_angles;
    egt::EllipseType<float> m_ellipse{};
    std::vector<std::string> m_lines;
    egt::PropertyAnimator m_animation;
    egt::AnimationSequence m_sequence{true};
    egt::PropertyAnimator m_swipe_animation{std::chrono::seconds(1),
            egt::easing_circular_easeout};
};

void LauncherItem::handle(egt::Event& event)
{
    egt::ImageLabel::handle(event);

    switch (event.id())
    {
    case egt::EventId::pointer_click:
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
    egt::Application app(argc, argv);

    // ensure max brightness of LCD screen
    egt::Application::instance().screen()->brightness(
        egt::Application::instance().screen()->max_brightness());

    egt::add_search_path(DATADIR "/egt/launcher/");
    egt::add_search_path("images/");

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
        std::ifstream in(egt::resolve_file_path("taglines.txt"), std::ios::binary);
        if (in.is_open())
            win.lines(in);
    }

    SwipeDetect swipe([&win](const std::string & direction)
    {
        if (direction == "right")
            win.move_boxes_swipe(true);
        else if (direction == "left")
            win.move_boxes_swipe(false);
    });

    // feed global events to swipe detector
    egt::Input::global_input().on_event([&swipe](egt::Event & event)
    {
        swipe.handle(event);
    });

    win.show();

    return app.run();
}
