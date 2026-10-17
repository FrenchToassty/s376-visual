#include "s376/geo.hpp"
#include "s376/log.hpp"
#include "s376/render.hpp"

#ifdef S376_HAS_OSG

#include <osg/Fog>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Light>
#include <osg/LightSource>
#include <osg/LineWidth>
#include <osg/Material>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>
#include <osg/Texture2D>
#include <osgDB/ReadFile>
#include <osgGA/GUIEventHandler>
#include <osgViewer/Viewer>

#ifdef S376_HAS_OSGEARTH
#include <osgEarth/EarthManipulator>
#include <osgEarth/MapNode>
#endif

#include <cmath>
#include <cstring>

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
            // Cheap land/water: a few rectangles covering the 17 airports' continents.
            bool land = false;
            if (lat > -60 && lat < 75) {
                if (lon > -130 && lon < -50 && lat > 15)
                    land = true; // N America
                if (lon > -90 && lon < -30 && lat > -55 && lat < 15)
                    land = true; // S America
                if (lon > -15 && lon < 50 && lat > 35)
                    land = true; // Europe
                if (lon > -20 && lon < 55 && lat > -35 && lat < 37)
                    land = true; // Africa
                if (lon > 40 && lon < 150 && lat > 5)
                    land = true; // Asia
                if (lon > 110 && lon < 155 && lat > -40 && lat < -10)
                    land = true; // Australia
            }
            if (lat > 70 || lat < -60)
                land = true; // ice
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
    // Tessellated ellipsoid. 96x48 is plenty for a hangar demo.
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

class OsgRenderer final : public Renderer {
  public:
    bool begin(const AppConfig& cfg, const ChannelConfig& channel) override {
        cfg_ = cfg;
        ch_ = channel;
        viewer_ = std::make_unique<osgViewer::Viewer>();
        viewer_->setThreadingModel(osgViewer::Viewer::SingleThreaded);

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
        (void)cfg;
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

        viewer_->setSceneData(root);
        viewer_->setUpViewInWindow(80, 80, channel.width, channel.height);
        viewer_->realize();
        if (auto* cam = viewer_->getCamera()) {
            cam->setClearColor(osg::Vec4(0.35f, 0.50f, 0.72f, 1));
            cam->setNearFarRatio(0.0001);
        }
        viewer_->getEventQueue()->keyPress(' '); // no-op to create queue
        S376_LOG_INFO("render", "OSG window %s %dx%d fov %.1fx%.1f hdg-off %.3f",
                      channel.name.c_str(), channel.width, channel.height, channel.hfov_deg,
                      channel.vfov_deg, channel.heading_offset_deg);
        return true;
    }

    void frame(const HostState& host) override {
        if (!viewer_)
            return;
        apply_camera(host);
        apply_env(host);
        viewer_->frame();
    }

    bool done() const override { return viewer_ && viewer_->done(); }
    void end() override { viewer_.reset(); }
    std::string name() const override { return used_earth_ ? "osgearth" : "osg"; }

  private:
    void apply_camera(const HostState& host) {
        if (!host.have_eyepoint)
            return;
        const double lat = host.eye.lat_deg;
        const double lon = host.eye.lon_deg;
        const double alt_m = host.eye.alt_ft * geo::kFtToM;
        const double hdg = host.eye.true_heading_deg() + ch_.heading_offset_deg;
        const double pitch = host.eye.pitch_deg + ch_.pitch_offset_deg;
        const double roll = host.eye.roll_deg + ch_.roll_offset_deg;

        const geo::Ecef eye = geo::lla_to_ecef(lat, lon, alt_m);
        geo::Enu fwd, right, down;
        geo::body_to_enu(hdg, pitch, roll, fwd, right, down);
        geo::Ecef east, north, up;
        geo::ecef_to_enu_axes(lat, lon, east, north, up);

        auto to_ecef = [&](const geo::Enu& b) {
            geo::Ecef o;
            o.x = b.e * east.x + b.n * north.x + b.u * up.x;
            o.y = b.e * east.y + b.n * north.y + b.u * up.y;
            o.z = b.e * east.z + b.n * north.z + b.u * up.z;
            return o;
        };
        const geo::Ecef f = to_ecef(fwd);
        const geo::Ecef d = to_ecef(down);
        // OSG camera looks down -Z, up is +Y. We look along +fwd, up is -down.
        const osg::Vec3d epos = ecef_to_osg(eye);
        const osg::Vec3d center = epos + osg::Vec3d(f.x, f.y, f.z) * 100.0;
        const osg::Vec3d upv(-d.x, -d.y, -d.z);

        auto* cam = viewer_->getCamera();
        const double aspect = static_cast<double>(ch_.width) / std::max(1, ch_.height);
        const double near_m = std::max(3.0, alt_m * 0.02);
        const double far_m = 2.0e7;
        cam->setProjectionMatrixAsPerspective(ch_.vfov_deg, aspect, near_m, far_m);
        cam->setViewMatrixAsLookAt(epos, center, upv);
    }

    void apply_env(const HostState& host) {
        float vis_ft = host.have_env ? host.env.visible_range_ft : 528000.f;
        if (host.have_fog && host.fog.select >= 0 && host.fog.rvr_ft > 0)
            vis_ft = std::min(vis_ft, host.fog.rvr_ft);
        vis_ft = std::max(200.f, std::min(vis_ft, xt4::kMaxVisibilityFt));
        const float vis_m = vis_ft * static_cast<float>(geo::kFtToM);
        // exp2: remaining ≈ exp(-(d/vis)^2 * 3) so ~5 % at the reported range.
        const float density = 1.7f / vis_m;
        fog_->setDensity(density);

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
        if (auto* cam = viewer_->getCamera()) {
            const float t = 0.25f + 0.55f * dayf;
            cam->setClearColor(
                osg::Vec4(0.15f + 0.25f * t, 0.22f + 0.35f * t, 0.35f + 0.40f * t, 1));
        }
    }

    AppConfig cfg_;
    ChannelConfig ch_;
    std::unique_ptr<osgViewer::Viewer> viewer_;
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
