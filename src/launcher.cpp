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

struct IconLayout
{
    egt::Rect box;
};

struct PagerLayout
{
    egt::Rect box;
    bool landscape;
    egt::Font::Size font_size;
    egt::StaticGrid::GridSize grid_size;
    egt::DefaultDim image_width;
    egt::DefaultDim image_height;
    egt::DefaultDim grid_padding;
    egt::DefaultDim grid_horizontal_space;
    egt::DefaultDim grid_vertical_space;
};

struct IndicatorLayout
{
    egt::Rect box;
};

struct LinesLayout
{
    egt::Rect box;
    egt::Font::Size font_size;
};

struct Layout
{
    egt::Size  screen_size;
    const char* background;
    struct IconLayout egt_logo;
    struct IconLayout mchp_logo;
    struct PagerLayout pager;
    struct IndicatorLayout indicator;
    struct LinesLayout lines;
};

static const struct Layout layouts[] =
{
    {
        egt::Size(800, 480),
        "background.png",
        { egt::Rect(203, 352, 128, 128) },
        { egt::Rect(469, 352, 128, 128) },
        {
            egt::Rect(0, 0, 800, 318),
            true,
            12.f,
            egt::StaticGrid::GridSize(6, 2),
            96, 96,
            32, 32, 32,
        },
        {
            egt::Rect(0, 300, 800, 32),
        },
        {
            egt::Rect(0, 332, 800, 20),
            18.f,
        },
    },

    {
        egt::Size(720, 1280),
        "background.png",
        { egt::Rect(176, 0, 128, 128) },
        { egt::Rect(416, 0, 128, 128) },
        {
            egt::Rect(0, 128, 720, 942),
            true,
            18.f,
            egt::StaticGrid::GridSize(3, 5),
            128, 128,
            32, 32, 32,
        },
        {
            egt::Rect(0, 1070, 720, 32),
        },
        {
            egt::Rect(0, 1134, 720, 146),
            18.f,
        },
    },

    /* sentinel */
    {
        egt::Size(),
        nullptr,
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

/**
 *
 */
class Pager : public egt::ScrolledView
{
public:
    Pager(const PagerLayout& layout,
          const std::function<void(size_t)>& on_page_changed) :
        ScrolledView(layout.box),
        m_landscape(layout.landscape),
        m_animator(std::chrono::milliseconds(1))
    {
        m_animator.on_change([this, on_page_changed](egt::DefaultDim value)
        {
            position(value);
            if (!m_animator.running())
            {
                size_t page_index = std::abs(value) / page_length();
                on_page_changed(page_index);
            }
        });

        font(egt::Font(layout.font_size));
    }
#if 0
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
                auto_scroll([] (float f) { return std::round(f); });
            }
            break;
        }
        default:
            break;
        }

        ScrolledView::handle(event);
    }
#else
    bool on_drag_start(egt::Event& event) override
    {
        m_animator.stop();
        return ScrolledView::on_drag_start(event);
    }

    void on_drag_stop(egt::Event& event) override
    {
        ScrolledView::on_drag_stop(event);
        if (!m_animator.running())
        {
            auto_scroll([] (float f) { return std::round(f); });
        }
    }
#endif
    void prev_page()
    {
        m_animator.stop();
        auto_scroll([] (float f) { return std::ceil(f); });
    }

    void next_page()
    {
        m_animator.stop();
        auto_scroll([] (float f) { return std::floor(f); });
    }

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
    bool m_landscape{true};
    egt::DefaultDim m_pixels_per_milliseconds{2};
    egt::PropertyAnimator m_animator;
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

/*
 * A launcher menu item.
 */
class LauncherItem : public egt::ImageLabel
{
public:
    LauncherItem(LauncherWindow& window,
                 // NOLINTNEXTLINE(modernize-pass-by-value)
                 const std::string& name, const std::string& description,
                 // NOLINTNEXTLINE(modernize-pass-by-value)
                 const std::string& image_path, const std::string& exec,
                 egt::DefaultDim width, egt::DefaultDim height)
        : egt::ImageLabel(egt::Image(image_path),
                          name,
                          egt::AlignFlag::center_horizontal | egt::AlignFlag::bottom),
          m_window(window),
          m_description(description),
          m_exec(exec)
    {
        autoresize(false);
        m_image.resize({width, height});
        align(egt::AlignFlag::expand);
        image_align(egt::AlignFlag::top);
        color(egt::Palette::ColorId::label_text, egt::Palette::white);
        fill_flags().clear();
    }

    void resize(const egt::Size& size) override
    {
        egt::ImageLabel::resize(size);
    }

    void handle(egt::Event& event) override;

private:
    LauncherWindow& m_window;
    std::string m_description;
    std::string m_exec;
};

const auto OFFSET_FILENAME = "/tmp/egt-launcher-offset";

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
        background(egt::Image(std::string("file:") + m_layout.background));

        auto logo = std::make_shared<egt::ImageLabel>(egt::Image("icon:microchip_logo_white.png;128"), "", layout.mchp_logo.box);
        add(logo);

        auto egt_logo = std::make_shared<egt::ImageLabel>(egt::Image("icon:egt_logo_white.png;128"), "", layout.egt_logo.box);
        add(egt_logo);

        auto cb = [this] (size_t page_index) { on_page_changed(page_index); };
        auto pager = std::make_shared<Pager>(m_layout.pager, cb);
        m_pager = pager.get();
        pager->fill_flags().clear();
        pager->vpolicy(egt::ScrolledView::Policy::never);
        pager->hpolicy(egt::ScrolledView::Policy::never);
        add(pager);

        auto orient = layout.pager.landscape ? egt::Orientation::horizontal : egt::Orientation::vertical;
        auto page_sizer = std::make_shared<egt::BoxSizer>(orient);
        m_page_sizer = page_sizer.get();
        page_sizer->align(egt::AlignFlag::top | egt::AlignFlag::left);
        pager->add(page_sizer);

        auto indicator_sizer = std::make_shared<egt::HorizontalBoxSizer>();
        m_indicator_sizer = indicator_sizer.get();
        indicator_sizer->box(layout.indicator.box);
        add(indicator_sizer);

        (void)add_page();
    }

    egt::StaticGrid* add_page()
    {
        const auto s = m_indicator_sizer->height();

        auto radio = std::make_shared<egt::RadioBox>();
        radio->disable();
        radio->show_label(false);
        radio->autoresize(false);
        radio->resize({s, s});
        m_indicator_group.add(radio);
        m_indicator_sizer->add(radio);

        auto grid = std::make_shared<egt::StaticGrid>(m_layout.pager.grid_size);
        grid->fill_flags().clear();
        grid->padding(m_layout.pager.grid_padding);
        grid->horizontal_space(m_layout.pager.grid_horizontal_space);
        grid->vertical_space(m_layout.pager.grid_vertical_space);
        grid->resize(m_pager->content_area().size());
        m_page_sizer->add(grid);
        return grid.get();
    }

    void add_item(const std::string& name,
                  const std::string& description,
                  const std::string& image_path,
                  const std::string& cmd)
    {
        egt::StaticGrid* page = nullptr;
        for (auto& child : m_page_sizer->children())
        {
            auto* p = static_cast<egt::StaticGrid*>(child.get());
            if (p->count_children() < (p->n_col() * p->n_row()))
            {
                page = p;
                break;
            }
        }

        if (!page)
            page = add_page();

        const auto w = m_layout.pager.image_width;
        const auto h = m_layout.pager.image_height;
        auto entry = std::make_shared<LauncherItem>(*this, name, description, image_path, cmd, w, h);
        page->add(entry);
    }

    void prev_page()
    {
        m_pager->prev_page();
    }

    void next_page()
    {
        m_pager->next_page();
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

        save_offset();

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

        add_item(name, description, "file:" + image, cmd);
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

    void load_offset()
    {
        egt::DefaultDim offset = 0;
        std::ifstream in(OFFSET_FILENAME);
        if (in.is_open())
            in >> offset;
        m_pager->position(offset);
    }

    void save_offset() const
    {
        auto offset = m_pager->position();
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
            auto vsizer = std::make_shared<egt::Frame>(m_layout.lines.box);
            add(vsizer);

            auto label = std::make_shared<egt::Label>();
            label->font(egt::Font(m_layout.lines.font_size));
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

private:

    const Layout& m_layout;
    egt::ButtonGroup m_indicator_group;
    Pager* m_pager{nullptr};
    egt::BoxSizer* m_page_sizer{nullptr};
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
    const auto* layout = &layouts[0];
    for (layout = layouts; !layout->screen_size.empty(); ++layout)
    {
        if (layout->screen_size == screen_size)
            break;
    }
    if (layout->screen_size.empty())
        layout = &layouts[0];

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

    win.load_offset();

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
    });

    win.show();

    return app.run();
}
