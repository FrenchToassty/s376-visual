#pragma once

#include "s376/config.hpp"
#include "s376/state.hpp"

#include <memory>
#include <string>

namespace s376 {

class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual bool begin(const AppConfig& cfg, const ChannelConfig& channel) = 0;
    virtual void frame(const HostState& host) = 0;
    virtual bool done() const = 0;
    virtual void end() = 0;
    virtual std::string name() const = 0;
};

// Always available. Advances no window; used by --headless and tests.
class NullRenderer : public Renderer {
  public:
    bool begin(const AppConfig&, const ChannelConfig&) override {
        started_ = true;
        return true;
    }
    void frame(const HostState&) override {}
    bool done() const override { return false; }
    void end() override { started_ = false; }
    std::string name() const override { return "null"; }

  private:
    bool started_ = false;
};

#ifdef S376_HAS_OSG
std::unique_ptr<Renderer> make_osg_renderer();
#endif

std::unique_ptr<Renderer> make_renderer(const std::string& engine);

} // namespace s376
