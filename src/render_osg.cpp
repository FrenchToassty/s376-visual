#include "s376/airport.hpp"
#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/log.hpp"
#include "s376/render.hpp"
#include "s376/terrain.hpp"

#ifdef S376_HAS_OSG

#include <osg/BlendFunc>
#include <osg/Depth>
#include <osg/DisplaySettings>
#include <osg/Fog>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/GraphicsContext>
#include <osg/Light>
#include <osg/LightSource>
#include <osg/Material>
#include <osg/Point>
#include <osg/PointSprite>
#include <osg/Texture2D>
#include <osg/Viewport>
#include <osgDB/ReadFile>
#include <osgViewer/CompositeViewer>
#include <osgViewer/Viewer>

#ifdef S376_HAS_OSGEARTH
#include <osgEarth/MapNode>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace s376 {
namespace {

osg::Vec3d ecef_to_osg(const geo::Ecef& p) {
    return osg::Vec3d(p.x, p.y, p.z);
}

osg::Image* make_globe_image(int w, int h) {
    auto* img = new osg::Image;
    img->allocateImage(w, h, 1, GL_RGB, GL_UNSIGNED_BYTE);
    auto* p = img->data();
    for (int y = 0; y < h; ++y) {
        const double lat = 90.0 - 180.0 * (y + 0.5) / h;
        for (int x = 0; x < w; ++x) {
            const double lon = -180.0 + 360.0 * (x + 0.5) / w;
            bool land = false;
            if (lat > -60 && lat < 75) {
                if (lon > -130 && lon < -50 && lat > 15)
                    land = true;
                if (lon > -90 && lon < -30 && lat > -55 && lat < 15)
                    land = true;
                if (lon > -15 && lon < 50 && lat > 35)
                    land = true;
                if (lon > -20 && lon < 55 && lat > -35 && lat < 37)
                    land = true;
                if (lon > 40 && lon < 150 && lat > 5)
                    land = true;
                if (lon > 110 && lon < 155 && lat > -40 && lat < -10)
                    land = true;
            }
            if (lat > 70 || lat < -60)
                land = true;
            const bool grid = (std::fmod(std::fabs(lat), 10.0) < 0.25) ||
                              (std::fmod(std::fabs(lon), 10.0) < 0.25);
            unsigned char r, g, b;
            if (grid) {
                r = g = b = 40;
            } else if (!land) {
                r = 20;
                g = 50;
                b = 110;
            } else if (lat > 70 || lat < -60) {
                r = g = b = 220;
            } else {
                r = 50;
                g = 110;
                b = 45;
            }
            const int i = (y * w + x) * 3;
            p[i] = r;
            p[i + 1] = g;
            p[i + 2] = b;
        }
    }
    return img;
}

osg::Node* make_procedural_globe() {
    auto* geode = new osg::Geode;
    constexpr int nlon = 96, nlat = 48;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    auto* norms = new osg::Vec3Array;
    auto* uvs = new osg::Vec2Array;
    verts->reserve((nlon + 1) * (nlat + 1));
    for (int j = 0; j <= nlat; ++j) {
        const double lat = 90.0 - 180.0 * j / nlat;
        for (int i = 0; i <= nlon; ++i) {
            const double lon = -180.0 + 360.0 * i / nlon;
            const geo::Ecef p = geo::lla_to_ecef(lat, lon, 0);
            verts->push_back(ecef_to_osg(p));
            geo::Ecef e, n, u;
            geo::ecef_to_enu_axes(lat, lon, e, n, u);
            norms->push_back(osg::Vec3(static_cast<float>(u.x), static_cast<float>(u.y),
                                       static_cast<float>(u.z)));
            uvs->push_back(
                osg::Vec2(static_cast<float>(i) / nlon, 1.f - static_cast<float>(j) / nlat));
        }
    }
    auto* idx = new osg::DrawElementsUInt(GL_TRIANGLES);
    for (int j = 0; j < nlat; ++j) {
        for (int i = 0; i < nlon; ++i) {
            const unsigned a = static_cast<unsigned>(j * (nlon + 1) + i);
            const unsigned b = a + 1;
            const unsigned c = a + static_cast<unsigned>(nlon + 1);
            const unsigned d = c + 1;
            idx->push_back(a);
            idx->push_back(c);
            idx->push_back(b);
            idx->push_back(b);
            idx->push_back(c);
            idx->push_back(d);
        }
    }
    geom->setVertexArray(verts);
    geom->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geom->setTexCoordArray(0, uvs);
    geom->addPrimitiveSet(idx);

    auto* tex = new osg::Texture2D(make_globe_image(720, 360));
    tex->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    geom->getOrCreateStateSet()->setTextureAttributeAndModes(0, tex, osg::StateAttribute::ON);
    auto* mat = new osg::Material;
    mat->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(1, 1, 1, 1));
    mat->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.15f, 0.15f, 0.18f, 1));
    geom->getOrCreateStateSet()->setAttributeAndModes(mat);
    geode->addDrawable(geom);
    return geode;
}

osg::Image* image_from_rgb(const ImageRgb& im) {
    auto* img = new osg::Image;
    img->allocateImage(im.w, im.h, 1, GL_RGB, GL_UNSIGNED_BYTE);
    if (!im.rgb.empty())
        std::memcpy(img->data(), im.rgb.data(), im.rgb.size());
    return img;
}

osg::Image* image_from_dds_or_rgb(const CacheRegion& cache) {
    if (!cache.imagery_dds.empty() && std::filesystem::exists(cache.imagery_dds)) {
        if (auto loaded = osgDB::readRefImageFile(cache.imagery_dds))
            return loaded.release();
        // OSG may lack a DDS plugin; decode BC1 ourselves.
        try {
            std::ifstream in(cache.imagery_dds, std::ios::binary);
            std::vector<char> hdr(128);
            in.read(hdr.data(), 128);
            const int w = static_cast<unsigned char>(hdr[16]) |
                          (static_cast<unsigned char>(hdr[17]) << 8) |
                          (static_cast<unsigned char>(hdr[18]) << 16) |
                          (static_cast<unsigned char>(hdr[19]) << 24);
            const int h = static_cast<unsigned char>(hdr[12]) |
                          (static_cast<unsigned char>(hdr[13]) << 8) |
                          (static_cast<unsigned char>(hdr[14]) << 16) |
                          (static_cast<unsigned char>(hdr[15]) << 24);
            const auto n = dxt::bc1_size(w, h);
            std::vector<std::uint8_t> blocks(n);
            in.read(reinterpret_cast<char*>(blocks.data()), static_cast<std::streamsize>(n));
            if (in) {
                auto rgb = dxt::decode_bc1(blocks.data(), w, h);
                ImageRgb im;
                im.w = w;
                im.h = h;
                im.rgb = std::move(rgb);
                return image_from_rgb(im);
            }
        } catch (...) {
        }
    }
    if (!cache.imagery.rgb.empty())
        return image_from_rgb(cache.imagery);
    return image_from_rgb(ImageRgb::hillshade(cache.dem));
}

osg::Node* make_dem_patch(const CacheRegion& cache) {
    const DemGrid& dem = cache.dem;
    if (dem.empty())
        return nullptr;
    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    auto* norms = new osg::Vec3Array;
    auto* uvs = new osg::Vec2Array;
    verts->reserve(static_cast<std::size_t>(dem.w * dem.h));
    for (int y = 0; y < dem.h; ++y) {
        const double lat = dem.north - (dem.north - dem.south) * y / std::max(1, dem.h - 1);
        for (int x = 0; x < dem.w; ++x) {
            const double lon = dem.west + (dem.east - dem.west) * x / std::max(1, dem.w - 1);
            const double z = dem.z_m[static_cast<std::size_t>(y * dem.w + x)];
            const geo::Ecef p = geo::lla_to_ecef(lat, lon, z);
            verts->push_back(ecef_to_osg(p));
            geo::Ecef e, n, u;
            geo::ecef_to_enu_axes(lat, lon, e, n, u);
            norms->push_back(osg::Vec3(static_cast<float>(u.x), static_cast<float>(u.y),
                                       static_cast<float>(u.z)));
            uvs->push_back(osg::Vec2(static_cast<float>(x) / std::max(1, dem.w - 1),
                                     static_cast<float>(y) / std::max(1, dem.h - 1)));
        }
    }
    auto* idx = new osg::DrawElementsUInt(GL_TRIANGLES);
    for (int y = 0; y < dem.h - 1; ++y) {
        for (int x = 0; x < dem.w - 1; ++x) {
            const unsigned a = static_cast<unsigned>(y * dem.w + x);
            const unsigned b = a + 1;
            const unsigned c = a + static_cast<unsigned>(dem.w);
            const unsigned d = c + 1;
            idx->push_back(a);
            idx->push_back(c);
            idx->push_back(b);
            idx->push_back(b);
            idx->push_back(c);
            idx->push_back(d);
        }
    }
    geom->setVertexArray(verts);
    geom->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geom->setTexCoordArray(0, uvs);
    geom->addPrimitiveSet(idx);

    auto* tex = new osg::Texture2D(image_from_dds_or_rgb(cache));
    tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    geom->getOrCreateStateSet()->setTextureAttributeAndModes(0, tex, osg::StateAttribute::ON);
    auto* mat = new osg::Material;
    mat->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4(1, 1, 1, 1));
    mat->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4(0.2f, 0.2f, 0.2f, 1));
    geom->getOrCreateStateSet()->setAttributeAndModes(mat);
    geode->addDrawable(geom);
    return geode;
}

osg::Node* make_airport_geode(const AirportMesh& mesh) {
    if (mesh.verts.size() < 3)
        return nullptr;
    auto* geode = new osg::Geode;
    auto* geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    auto* norms = new osg::Vec3Array;
    auto* cols = new osg::Vec4Array;
    verts->reserve(mesh.verts.size());
    for (const auto& v : mesh.verts) {
        verts->push_back(ecef_to_osg(v.p));
        norms->push_back(osg::Vec3(static_cast<float>(v.n.x), static_cast<float>(v.n.y),
                                   static_cast<float>(v.n.z)));
        cols->push_back(osg::Vec4(v.r, v.g, v.b, 1));
    }
    geom->setVertexArray(verts);
    geom->setNormalArray(norms, osg::Array::BIND_PER_VERTEX);
    geom->setColorArray(cols, osg::Array::BIND_PER_VERTEX);
    geom->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, static_cast<int>(mesh.verts.size())));
    auto* mat = new osg::Material;
    mat->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
    geom->getOrCreateStateSet()->setAttributeAndModes(mat);
    geode->addDrawable(geom);
    return geode;
}

struct LightGpu {
    osg::ref_ptr<osg::Geometry> geom;
    osg::ref_ptr<osg::Vec4Array> colors;
    std::vector<int> switches;
    std::vector<osg::Vec4> base;
};

LightGpu make_light_gpu(const Airport& apt) {
    LightGpu g;
    g.geom = new osg::Geometry;
    auto* verts = new osg::Vec3Array;
    g.colors = new osg::Vec4Array;
    verts->reserve(apt.lights.size());
    for (const auto& L : apt.lights) {
        const geo::Ecef p = geo::lla_to_ecef(L.lat, L.lon, L.alt_m);
        verts->push_back(ecef_to_osg(p));
        const osg::Vec4 c(L.r, L.g, L.b, 1);
        g.colors->push_back(c);
        g.base.push_back(c);
        g.switches.push_back(L.switch_num);
    }
    g.geom->setVertexArray(verts);
    g.geom->setColorArray(g.colors, osg::Array::BIND_PER_VERTEX);
    g.geom->addPrimitiveSet(
        new osg::DrawArrays(GL_POINTS, 0, static_cast<int>(apt.lights.size())));
    g.geom->setUseDisplayList(false);
    g.geom->setUseVertexBufferObjects(true);
    auto* ss = g.geom->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    ss->setMode(GL_BLEND, osg::StateAttribute::ON);
    ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE));
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0, 1, false));
    ss->setAttributeAndModes(new osg::Point(5.0f));
    ss->setTextureAttributeAndModes(0, new osg::PointSprite, osg::StateAttribute::ON);
    ss->setMode(GL_POINT_SMOOTH, osg::StateAttribute::ON);
    ss->setRenderBinDetails(20, "DepthSortedBin");
    return g;
}

void apply_light_steps(LightGpu& g, const HostState& host) {
    if (!g.colors || g.colors->empty())
        return;
    for (std::size_t i = 0; i < g.colors->size(); ++i) {
        int step = 3;
        if (i < g.switches.size()) {
            const int sw = g.switches[i];
            if (sw >= 0 && sw < xt4::kMaxLightSwitches)
                step = host.light_steps[static_cast<std::size_t>(sw)];
        }
        const float k = step <= 0 ? 0.f : std::min(1.f, step / 5.f);
        const osg::Vec4& b = g.base[i];
        (*g.colors)[static_cast<unsigned>(i)] = osg::Vec4(b.r() * k, b.g() * k, b.b() * k, k);
    }
    g.colors->dirty();
    g.geom->dirtyBound();
}

CacheRegion load_or_synth_cache(const std::string& dir) {
    if (!dir.empty()) {
        try {
            auto r = load_cache(dir);
            S376_LOG_INFO("render", "loaded DEM cache %s  %dx%d", dir.c_str(), r.dem.w, r.dem.h);
            return r;
        } catch (const std::exception& ex) {
            S376_LOG_WARN("render", "cache %s: %s — using synth KSEA DEM", dir.c_str(), ex.what());
        }
    }
    auto r = generate_ksea_cache();
    S376_LOG_INFO("render", "synth KSEA DEM %dx%d (run s376-cache for a packed region)", r.dem.w,
                  r.dem.h);
    return r;
}

void apply_view_camera(osg::Camera* cam, const ChannelConfig& ch, const HostState& host) {
    if (!cam || !host.have_eyepoint)
        return;
    const auto v = geo::make_channel_view(
        host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM,
        host.eye.true_heading_deg(), host.eye.pitch_deg, host.eye.roll_deg, ch.heading_offset_deg,
        ch.pitch_offset_deg, ch.roll_offset_deg, ch.hfov_deg, ch.vfov_deg, ch.width, ch.height);
    const osg::Vec3d epos = ecef_to_osg(v.eye);
    const osg::Vec3d center =
        epos + osg::Vec3d(v.forward.x, v.forward.y, v.forward.z) * 100.0;
    const osg::Vec3d upv(v.up.x, v.up.y, v.up.z);
    cam->setProjectionMatrixAsPerspective(v.vfov_deg, v.aspect, v.near_m, v.far_m);
    cam->setViewMatrixAsLookAt(epos, center, upv);
}

class OsgRenderer final : public Renderer {
  public:
    bool begin(const AppConfig& cfg, const std::vector<ChannelConfig>& channels) override {
        cfg_ = cfg;
        channels_ = channels;
        if (channels_.empty()) {
            S376_LOG_ERROR("render", "no channels to open");
            return false;
        }

        viewer_ = std::make_unique<osgViewer::CompositeViewer>();
        viewer_->setThreadingModel(osgViewer::ViewerBase::SingleThreaded);
        // Apt OSG 3.6 is a compatibility-profile stack. Ask for 3.3 so a
        // Maxwell GTX 980 Ti (GL 4.6 max) and the hangar RTX 5060 Ti share
        // the same path. Do not request a Core profile.
        if (osg::ref_ptr<osg::DisplaySettings> ds = osg::DisplaySettings::instance())
            ds->setGLContextVersion("3.3");

        osg::ref_ptr<osg::Group> root = new osg::Group;
#ifdef S376_HAS_OSGEARTH
        osg::ref_ptr<osg::Node> earth = osgDB::readRefNodeFile(cfg.render.earth_file);
        if (earth) {
            root->addChild(earth);
            used_earth_ = true;
            S376_LOG_INFO("render", "loaded osgEarth map %s", cfg.render.earth_file.c_str());
        } else {
            S376_LOG_WARN("render", "could not load %s; using procedural globe",
                          cfg.render.earth_file.c_str());
            root->addChild(make_procedural_globe());
        }
#else
        root->addChild(make_procedural_globe());
#endif
        if (!used_earth_) {
            const std::string dir = cfg.render.cache_dir;
            bool any_lod = false;
            if (!dir.empty()) {
                for (int i = 2; i >= 0; --i) {
                    const std::string lod = dir + "/lod" + std::to_string(i);
                    if (!std::filesystem::exists(lod + "/elev.s376dem"))
                        continue;
                    try {
                        auto cache = load_cache(lod);
                        if (auto* patch = make_dem_patch(cache)) {
                            root->addChild(patch);
                            any_lod = true;
                            S376_LOG_INFO("render", "draped LOD%d  %dx%d  %.3f,%.3f .. %.3f,%.3f", i,
                                          cache.dem.w, cache.dem.h, cache.dem.west, cache.dem.south,
                                          cache.dem.east, cache.dem.north);
                        }
                    } catch (const std::exception& ex) {
                        S376_LOG_WARN("render", "LOD%d: %s", i, ex.what());
                    }
                }
            }
            if (!any_lod) {
                const CacheRegion cache = load_or_synth_cache(dir);
                if (auto* patch = make_dem_patch(cache)) {
                    root->addChild(patch);
                    S376_LOG_INFO("render", "draped %s DEM  %.3f,%.3f .. %.3f,%.3f  BC1/hillshade",
                                  cache.name.empty() ? "region" : cache.name.c_str(), cache.dem.west,
                                  cache.dem.south, cache.dem.east, cache.dem.north);
                }
            }
        }
        {
            Airport merged;
            for (Airport apt : all_public_airports()) {
                const int osm_n = try_load_osm(apt, cfg.render.cache_dir);
                if (osm_n > 0)
                    S376_LOG_INFO("render", "%s OSM aeroway  %d ways", apt.icao.c_str(), osm_n);
                if (auto* mesh = make_airport_geode(build_airport_mesh(apt))) {
                    root->addChild(mesh);
                    S376_LOG_INFO("render", "%s mesh  %zu runways  %zu lights  %s", apt.icao.c_str(),
                                  apt.runways.size(), apt.lights.size(), apt.attribution.c_str());
                }
                merged.lights.insert(merged.lights.end(), apt.lights.begin(), apt.lights.end());
            }
            lights_gpu_ = make_light_gpu(merged);
            if (lights_gpu_.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(lights_gpu_.geom);
                root->addChild(geode);
            }
        }
        light_ = new osg::LightSource;
        auto* L = light_->getLight();
        L->setLightNum(0);
        L->setAmbient(osg::Vec4(0.08f, 0.08f, 0.1f, 1));
        L->setDiffuse(osg::Vec4(1.0f, 0.95f, 0.85f, 1));
        L->setPosition(osg::Vec4(1, 0, 0, 0));
        root->addChild(light_);
        root->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);
        root->getOrCreateStateSet()->setMode(GL_LIGHT0, osg::StateAttribute::ON);

        fog_ = new osg::Fog;
        fog_->setMode(osg::Fog::EXP2);
        fog_->setColor(osg::Vec4(0.62f, 0.70f, 0.78f, 1));
        fog_->setDensity(1e-6f);
        root->getOrCreateStateSet()->setAttributeAndModes(fog_);
        root->getOrCreateStateSet()->setMode(GL_FOG, osg::StateAttribute::ON);
        root_ = root;

        int nscr = 1, sw = 1920, sh = 1080;
        if (auto* wsi = osg::GraphicsContext::getWindowingSystemInterface()) {
            nscr = static_cast<int>(std::max(1u, wsi->getNumScreens()));
            osg::GraphicsContext::ScreenSettings ss;
            wsi->getScreenSettings(osg::GraphicsContext::ScreenIdentifier(0), ss);
            if (ss.width > 0)
                sw = ss.width;
            if (ss.height > 0)
                sh = ss.height;
        }
        const auto places = layout_channels(channels_, nscr, sw, sh);

        for (std::size_t i = 0; i < channels_.size(); ++i) {
            const auto& ch = channels_[i];
            WindowPlacement place = places[i];
            for (const auto& p : places) {
                if (p.name == ch.name)
                    place = p;
            }

            osg::ref_ptr<osgViewer::View> view = new osgViewer::View;
            view->setSceneData(root);
            // setUpViewInWindow handles X11 DISPLAY / pixel format; raw Traits
            // often fail under Xvfb.
            if (place.fullscreen)
                view->setUpViewOnSingleScreen(static_cast<unsigned>(place.screen));
            else
                view->setUpViewInWindow(place.x, place.y, place.w, place.h,
                                        static_cast<unsigned>(place.screen));
            if (auto* cam = view->getCamera()) {
                cam->setClearColor(osg::Vec4(0.35f, 0.50f, 0.72f, 1));
                cam->setNearFarRatio(0.0001);
                if (auto* vp = cam->getViewport()) {
                    place.w = static_cast<int>(vp->width());
                    place.h = static_cast<int>(vp->height());
                }
            }
            viewer_->addView(view);
            views_.push_back(view);

            S376_LOG_INFO("render",
                          "channel %s  %dx%d at %d,%d screen %d  fov %.1fx%.1f  hdg-off %.3f%s",
                          ch.name.c_str(), place.w, place.h, place.x, place.y, place.screen,
                          ch.hfov_deg, ch.vfov_deg, ch.heading_offset_deg,
                          channels_.size() == 1 ? " (single-window)" : "");
        }

        viewer_->realize();
        S376_LOG_INFO("render", "opened %zu channel window(s)  screens=%d  %dx%d", views_.size(),
                      nscr, sw, sh);
        return true;
    }

    void frame(const HostState& host) override {
        if (!viewer_)
            return;
        for (std::size_t i = 0; i < views_.size() && i < channels_.size(); ++i)
            apply_view_camera(views_[i]->getCamera(), channels_[i], host);
        apply_env(host);
        apply_light_steps(lights_gpu_, host);
        viewer_->frame();
    }

    bool done() const override { return viewer_ && viewer_->done(); }
    void end() override {
        views_.clear();
        viewer_.reset();
        root_ = nullptr;
    }
    std::string name() const override { return used_earth_ ? "osgearth" : "osg"; }
    std::size_t view_count() const override { return views_.size(); }

  private:
    void apply_env(const HostState& host) {
        float vis_ft = host.have_env ? host.env.visible_range_ft : 528000.f;
        if (host.have_fog && host.fog.select >= 0 && host.fog.rvr_ft > 0)
            vis_ft = std::min(vis_ft, host.fog.rvr_ft);
        vis_ft = std::max(200.f, std::min(vis_ft, xt4::kMaxVisibilityFt));
        const float vis_m = vis_ft * static_cast<float>(geo::kFtToM);
        fog_->setDensity(1.7f / vis_m);

        const float tod = host.have_env ? host.env.tod_hours : 14.5f;
        const int month = host.have_env ? host.env.month : 6;
        const int day = host.have_env ? host.env.day : 21;
        const double lat = host.have_eyepoint ? host.eye.lat_deg : 47.46;
        const double lon = host.have_eyepoint ? host.eye.lon_deg : -122.31;
        double el = 0;
        const geo::Ecef s = geo::sun_ecef(lat, lon, tod, month, day, &el);
        auto* L = light_->getLight();
        L->setPosition(osg::Vec4(static_cast<float>(s.x), static_cast<float>(s.y),
                                 static_cast<float>(s.z), 0));
        const float dayf = static_cast<float>(std::max(0.0, std::sin(geo::deg2rad(el))));
        L->setDiffuse(osg::Vec4(1.0f, 0.95f, 0.85f, 1) * (0.15f + 0.85f * dayf));
        L->setAmbient(
            osg::Vec4(0.04f + 0.12f * dayf, 0.04f + 0.12f * dayf, 0.06f + 0.12f * dayf, 1));
        const float t = 0.25f + 0.55f * dayf;
        const osg::Vec4 sky(0.15f + 0.25f * t, 0.22f + 0.35f * t, 0.35f + 0.40f * t, 1);
        for (auto& view : views_) {
            if (auto* cam = view->getCamera())
                cam->setClearColor(sky);
        }
    }

    AppConfig cfg_;
    std::vector<ChannelConfig> channels_;
    std::unique_ptr<osgViewer::CompositeViewer> viewer_;
    std::vector<osg::ref_ptr<osgViewer::View>> views_;
    osg::ref_ptr<osg::Group> root_;
    osg::ref_ptr<osg::LightSource> light_;
    osg::ref_ptr<osg::Fog> fog_;
    LightGpu lights_gpu_;
    bool used_earth_ = false;
};

} // namespace

std::unique_ptr<Renderer> make_osg_renderer() {
    return std::make_unique<OsgRenderer>();
}

} // namespace s376

#endif // S376_HAS_OSG
