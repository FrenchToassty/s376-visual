#include "s376/geo.hpp"
#include "s376/log.hpp"
#include "s376/render.hpp"

#ifdef S376_HAS_OSG

#include <osg/Fog>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/GraphicsContext>
#include <osg/Light>
#include <osg/LightSource>
#include <osg/Material>
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
    bool used_earth_ = false;
};

} // namespace

std::unique_ptr<Renderer> make_osg_renderer() {
    return std::make_unique<OsgRenderer>();
}

} // namespace s376

#endif // S376_HAS_OSG
