#pragma once

#include "s376/config.hpp"
#include "s376/state.hpp"

#include <memory>
#include <string>
#include <vector>

namespace s376 {

class Renderer {
  public:
    virtual ~Renderer() = default;
    // `channels` is the already-selected list (one window, or all enabled).
    virtual bool begin(const AppConfig& cfg, const std::vector<ChannelConfig>& channels) = 0;
    virtual void frame(const HostState& host) = 0;
    virtual bool done() const = 0;
    virtual void end() = 0;
    virtual std::string name() const = 0;
    virtual std::size_t view_count() const { return 0; }
};

class NullRenderer : public Renderer {
  public:
    bool begin(const AppConfig&, const std::vector<ChannelConfig>& channels) override {
        n_ = channels.size();
        return true;
    }
    void frame(const HostState&) override {}
    bool done() const override { return false; }
    void end() override { n_ = 0; }
    std::string name() const override { return "null"; }
    std::size_t view_count() const override { return n_; }

  private:
    std::size_t n_ = 0;
};

#ifdef S376_HAS_OSG
std::unique_ptr<Renderer> make_osg_renderer();
#endif

std::unique_ptr<Renderer> make_renderer(const std::string& engine);

} // namespace s376
