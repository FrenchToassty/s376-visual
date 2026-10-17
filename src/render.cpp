#include "s376/render.hpp"

#include <algorithm>
#include <cctype>

namespace s376 {

std::unique_ptr<Renderer> make_renderer(const std::string& engine) {
    std::string e = engine;
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#ifdef S376_HAS_OSG
    if (e == "auto" || e == "osg" || e == "osgearth")
        return make_osg_renderer();
#endif
    return std::make_unique<NullRenderer>();
}

} // namespace s376
