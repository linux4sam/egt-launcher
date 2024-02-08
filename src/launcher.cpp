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
#include <filesystem>
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

struct Layout
{
    bool landscape;
    const char* background;
    egt::Serializer::Properties egt_logo;
    egt::Serializer::Properties mchp_logo;
    egt::Serializer::Properties pager;
    egt::Serializer::Properties grid;
    egt::Serializer::Properties item;
    egt::Serializer::Properties indicator;
    egt::Serializer::Properties lines;
};

static const Layout landscape_layout =
{
    true,
    "background_800x480.png",
    /* egt_logo */
    {
        { "ratio:vertical", "10", {} },
        { "ratio:horizontal", "50", {} },
        { "align", "bottom|left", {} },
    },
    /* mchp_logo */
    {
        { "ratio:vertical", "10", {} },
        { "ratio:horizontal", "50", {} },
        { "align", "bottom|right", {} },
    },
    /* pager */
    {
        { "landscape", "true", {} },
        { "ratio:vertical", "66", {} },
        { "align", "top|expand_horizontal", {} },
        { "horizontal_policy", "never", {} },
        { "vertical_policy", "never", {} },
    },
    /* grid */
    {
        { "n_col", "6", {} },
        { "n_row", "2", {} },
        { "padding", "32", {} },
        { "horizontal_space", "32", {} },
        { "vertical_space", "32", {} },
    },
    /* item */
    {
        { "color", "ffffffff", { { "id", "label_text" }, { "group", "normal" } } },
    },
    /* indicator */
    {
        { "ratio:y", "66", {} },
        { "ratio:vertical", "7", {} },
        { "align", "center_horizontal", {} },
    },
    /* lines */
    {
        { "ratio:y", "73", {} },
        { "ratio:vertical", "17", {} },
        { "color", "ffffffff", { { "id", "label_text" }, { "group", "normal" } } },
    },
};

static const Layout portrait_layout =
{
    false,
    "background_720x1280.png",
    /* egt_logo */
    {
        { "ratio:vertical", "4", {} },
        { "ratio:horizontal", "50", {} },
        { "align", "top|left", {} },
    },
    /* mchp_logo */
    {
        { "ratio:vertical", "4", {} },
        { "ratio:horizontal", "50", {} },
        { "align", "top|right", {} },
    },
    /* pager */
    {
        { "landscape", "true", {} },
        { "ratio:y", "4", {} },
        { "ratio:vertical", "75", {} },
        { "align", "expand_horizontal", {} },
        { "horizontal_policy", "never", {} },
        { "vertical_policy", "never", {} },
    },
    /* grid */
    {
        { "n_col", "3", {} },
        { "n_row", "5", {} },
        { "padding", "32", {} },
        { "horizontal_space", "32", {} },
        { "vertical_space", "32", {} },
    },
    /* item */
    {
        { "color", "ffffffff", { { "id", "label_text" }, { "group", "normal" } } },
    },
    /* indicator */
    {
        { "ratio:y", "79", {} },
        { "ratio:vertical", "3", {} },
        { "align", "center_horizontal", {} },
    },
    /* lines */
    {
        { "ratio:y", "81", {} },
        { "ratio:vertical", "19", {} },
        { "color", "ffffffff", { { "id", "label_text" }, { "group", "normal" } } },
    },
};

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
 * Add a property.
 */
static egt::Serializer::Properties& add_prop(egt::Serializer::Properties& props,
        const std::string& pname,
        const std::string& pvalue,
        const egt::Serializer::Attributes& attrs = {})
{
    props.emplace_back(std::make_tuple(pname, pvalue, attrs));
    return props;
}

/*
 * A launcher menu item.
 */
class LauncherItem : public egt::ImageLabel
{
public:
    LauncherItem(egt::Serializer::Properties& props,
                 LauncherWindow& window)
        : egt::ImageLabel(props, true),
          m_window(window)
    {
        deserialize(props);

        deserialize_leaf(props);
    }

    void handle(egt::Event& event) override;

private:

    void deserialize(egt::Serializer::Properties& props)
    {
        props.erase(std::remove_if(props.begin(), props.end(), [&](auto & p)
        {
            bool ret = true;
            auto value = std::get<1>(p);
            switch (egt::detail::hash(std::get<0>(p)))
            {
            case egt::detail::hash("description"):
                m_description = value;
                break;
            case egt::detail::hash("exec"):
                m_exec = value;
                break;
            default:
                ret = false;
                break;
            }
            return ret;
        }), props.end());
    }

    LauncherWindow& m_window;
    std::string m_description;
    std::string m_exec;
};

/**
 *
 */
class Pager : public egt::ScrolledView
{
public:
    using PageAddedCallback = std::function<void (void)>;
    using PageChangedCallback = std::function<void (size_t)>;

    Pager(egt::Serializer::Properties& props,
          const egt::Serializer::Properties& grid_props,
          const PageAddedCallback& on_page_added,
          const PageChangedCallback& on_page_changed) :
        ScrolledView(props, true),
        m_grid_props(grid_props),
        m_sizer(egt::Orientation::horizontal, egt::Justification::start),
        m_animator(std::chrono::milliseconds(1)),
        m_on_page_added(on_page_added),
        m_on_page_changed(on_page_changed)
    {
        m_animator.on_change([this](egt::DefaultDim value)
        {
            position(value);
            if (!m_animator.running())
                m_on_page_changed(page());
        });

        deserialize(props);

        auto orient = m_landscape ? egt::Orientation::horizontal : egt::Orientation::vertical;
        m_sizer.orient(orient);
        m_sizer.align(egt::AlignFlag::top | egt::AlignFlag::left);
        add(m_sizer);

        deserialize_leaf(props);
    }

    void handle(egt::Event& event) override
    {
        switch (event.id())
        {
        case egt::EventId::pointer_drag_start:
            m_animator.stop();
            break;
        case egt::EventId::pointer_drag_stop:
        {
            if (!m_animator.running())
            {
                auto_scroll([](float f) { return std::round(f); });
            }
            break;
        }
        default:
            break;
        }

        ScrolledView::handle(event);
    }

    void page(size_t page_index)
    {
        m_animator.stop();
        position(-page_index * page_length());
        m_on_page_changed(page());
    }

    size_t page() const
    {
        return std::abs(position()) / page_length();
    }

    void prev_page()
    {
        m_animator.stop();
        auto_scroll([](float f) { return std::ceil(f); });
    }

    void next_page()
    {
        m_animator.stop();
        auto_scroll([](float f) { return std::floor(f); });
    }

    void add_item(const std::shared_ptr<Widget>& item)
    {
        egt::StaticGrid* page = first_available_page();
        if (!page)
            page = add_page();

        page->add(item);
    }

protected:

    egt::StaticGrid* add_page()
    {
        auto props = m_grid_props;
        auto grid = std::make_shared<egt::StaticGrid>(props);
        grid->resize(content_area().size());
        m_sizer.add(grid);
        m_on_page_added();
        return grid.get();
    }

    egt::StaticGrid* first_available_page() const
    {
        for (auto& child : m_sizer.children())
        {
            auto* p = static_cast<egt::StaticGrid*>(child.get());
            if (p->count_children() < (p->n_col() * p->n_row()))
                return p;
        }

        return nullptr;
    }

    void position(egt::DefaultDim value)
    {
        auto p = offset();
        if (m_landscape)
            p.x(value);
        else
            p.y(value);
        offset(p);
    }

    EGT_NODISCARD egt::DefaultDim position() const { return to_dim(offset()); }

    EGT_NODISCARD egt::DefaultDim page_length() const { return to_dim(content_area().size()); }

    void auto_scroll(const std::function<float(float)>& func)
    {
        const auto plen = page_length();
        const auto start = position();
        const auto end = plen * static_cast<egt::DefaultDim>(func(static_cast<float>(start) / static_cast<float>(plen)));
        m_animator.duration(std::chrono::milliseconds(std::abs(end - start) / m_pixels_per_milliseconds));
        m_animator.starting(start);
        m_animator.ending(end);
        m_animator.start();
    }

    EGT_NODISCARD egt::DefaultDim to_dim(const egt::Point& p) const
    {
        if (m_landscape)
            return p.x();

        return p.y();
    }

    egt::DefaultDim to_dim(const egt::Size& s) const
    {
        if (m_landscape)
            return s.width();

        return s.height();
    }

private:

    void deserialize(egt::Serializer::Properties& props)
    {
        props.erase(std::remove_if(props.begin(), props.end(), [&](auto & p)
        {
            bool ret = true;
            auto value = std::get<1>(p);
            switch (egt::detail::hash(std::get<0>(p)))
            {
            case egt::detail::hash("landscape"):
                m_landscape = egt::detail::from_string(value);
                break;
            case egt::detail::hash("pixels_per_milliseconds"):
                m_pixels_per_milliseconds = std::stoi(value);
                break;
            default:
                ret = false;
                break;
            }
            return ret;
        }), props.end());
    }

    const egt::Serializer::Properties& m_grid_props;
    egt::BoxSizer m_sizer;
    egt::PropertyAnimator m_animator;
    PageAddedCallback m_on_page_added;
    PageChangedCallback m_on_page_changed;

    bool m_landscape{true};
    egt::DefaultDim m_pixels_per_milliseconds{2};
};

const auto PAGE_FILENAME = "/tmp/egt-launcher-page";

/**
 * Main launcher window.
 */
class LauncherWindow : public egt::TopWindow
{
public:
    LauncherWindow(const Layout& layout) :
        m_layout(layout),
        m_indicator_group(true, true)
    {
        /* If not visible, layout() is not executed when adding child. */
        show();

        background(egt::Image(std::string("file:") + m_layout.background));

        auto mchp_logo_props = m_layout.mchp_logo;
        add_prop(mchp_logo_props, "image", "icon:microchip_logo_white.png;128");
        add_prop(mchp_logo_props, "showlabel", "false");
        add_prop(mchp_logo_props, "image_align", "center|expand");
        auto logo = std::make_shared<egt::ImageLabel>(mchp_logo_props);
        add(logo);

        auto egt_logo_props = m_layout.egt_logo;
        add_prop(egt_logo_props, "image", "icon:egt_logo_white.png;128");
        add_prop(egt_logo_props, "showlabel", "false");
        add_prop(egt_logo_props, "image_align", "center|expand");
        auto egt_logo = std::make_shared<egt::ImageLabel>(egt_logo_props);
        add(egt_logo);

        auto indicator_props = m_layout.indicator;
        auto indicator_sizer = std::make_shared<egt::BoxSizer>(indicator_props);
        m_indicator_sizer = indicator_sizer.get();
        add(indicator_sizer);

        auto pager_props = m_layout.pager;
        auto padded = [this]() { on_page_added(); };
        auto pchanged = [this](size_t page_index) { on_page_changed(page_index); };
        auto pager = std::make_shared<Pager>(pager_props, m_layout.grid, padded, pchanged);
        m_pager = pager.get();
        add(pager);
    }

    void prev_page()
    {
        m_pager->prev_page();
    }

    void next_page()
    {
        m_pager->next_page();
    }

    void on_page_added()
    {
        const auto s = m_indicator_sizer->height();

        auto radio = std::make_shared<egt::RadioBox>();
        radio->disable();
        radio->show_label(false);
        radio->autoresize(false);
        radio->resize({s, s});
        m_indicator_group.add(radio);
        m_indicator_sizer->add(radio);
    }

    void on_page_changed(size_t page_index)
    {
        auto& radio = *static_cast<egt::RadioBox*>(m_indicator_sizer->child_at(page_index).get());
        radio.checked(true);
    }

    void launch(const std::string& exe) const
    {
        egt::Application::instance().event().quit();

#ifdef HAVE_EGT_DETAIL_SCREEN_KMSSCREEN_H
        // explicitly close KMS
        if (egt::detail::KMSScreen::instance())
            egt::detail::KMSScreen::instance()->close();
#endif

        save_page_index();

        const std::string cmd = DATADIR "/egt/launcher/launch.sh " + exe + " &";
        exec(cmd.c_str());
    }

    static std::vector<std::string> get_files(const std::string& dir)
    {
        std::vector<std::string> files;

        try
        {
            if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir))
            {
                std::filesystem::recursive_directory_iterator iter(dir);
                std::filesystem::recursive_directory_iterator end;

                while (iter != end)
                {
                    if (!std::filesystem::is_directory(iter->path().string()))
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

        const egt::Font::Size font_size = scale(11.f, 20.f);
        const egt::DefaultDim image_size = scale(96.f, 96.f);

        auto props = m_layout.item;
        add_prop(props, "text", name);
        add_prop(props, "image", "file:" + image,
        {
            { "keep_image_ratio", "false" },
        });
        add_prop(props, "description", description);
        add_prop(props, "exec", cmd);
        add_prop(props, "align", "expand");
        add_prop(props, "text_align", "center_horizontal|bottom");
        add_prop(props, "image_align", "top");
        add_prop(props, "font", "FreeSans",
        {
            { "weight", "normal" },
            { "slant", "normal" },
            { "size", std::to_string(font_size) },
        });
        auto item = std::make_shared<LauncherItem>(props, *this);
        item->image().resize(egt::Size(image_size, image_size));
        m_pager->add_item(item);
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

        return 0;
    }

    void load_page_index()
    {
        size_t page = 0;
        std::ifstream in(PAGE_FILENAME);
        if (in.is_open())
            in >> page;
        m_pager->page(page);
    }

    void save_page_index() const
    {
        auto page = m_pager->page();
        std::ofstream out(PAGE_FILENAME, std::ios::trunc);
        if (out.is_open())
            out << page;
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
            const egt::Font::Size font_size = scale(18.f, 22.f);

            auto props = m_layout.lines;
            add_prop(props, "x", "0");
            add_prop(props, "width", std::to_string(width()));
            add_prop(props, "ratio:horizontal", "100");
            add_prop(props, "font", "FreeSans",
            {
                { "weight", "normal" },
                { "slant", "normal" },
                { "size", std::to_string(font_size) },
            });
            auto vsizer = std::make_shared<egt::Frame>(props);
            add(vsizer);

            auto label = std::make_shared<egt::Label>();
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

protected:

    float scale(float landscape_value, float portrait_value) const
    {
        if (m_layout.landscape)
            return (landscape_value * height()) / 480.f;

        return (portrait_value * height()) / 1280.f;
    }

private:

    const Layout& m_layout;
    egt::ButtonGroup m_indicator_group;
    Pager* m_pager{nullptr};
    egt::BoxSizer* m_indicator_sizer{nullptr};
    std::vector<std::string> m_lines;
    egt::AnimationSequence m_sequence{true};
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

    // select the application layout
    const auto screen_size = app.screen()->size();
    const auto landscape = screen_size.width() >= screen_size.height();
    const auto* layout = landscape ? &landscape_layout : &portrait_layout;

    egt::add_search_path(DATADIR "/egt/launcher/");
    egt::add_search_path("images/");

    LauncherWindow win(*layout);

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

    win.load_page_index();

    {
        std::ifstream in(egt::resolve_file_path("taglines.txt"), std::ios::binary);
        if (in.is_open())
            win.lines(in);
    }

    SwipeDetect swipe([&win](const std::string & direction)
    {
        if (direction == "right")
            win.next_page();
        else if (direction == "left")
            win.prev_page();
    });

    // feed global events to swipe detector
    egt::Input::global_input().on_event([&swipe](egt::Event & event)
    {
        swipe.handle(event);
    }, {egt::EventId::raw_pointer_down, egt::EventId::raw_pointer_up});

    win.show();

    return app.run();
}
