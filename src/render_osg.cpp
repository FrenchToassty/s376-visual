#include "s376/airport.hpp"
#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/log.hpp"
#include "s376/render.hpp"
#include "s376/terrain.hpp"

#ifdef S376_HAS_OSG

#include <osg/BlendFunc>
#include <osg/CullFace>
#include <osg/Depth>
#include <osg/DisplaySettings>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/GraphicsContext>
#include <osg/Notify>
#include <osg/Point>
#include <osg/Program>
#include <osg/Shader>
#include <osg/State>
#include <osg/Texture2D>
#include <osg/Uniform>
#include <osg/Viewport>
#include <osgDB/ReadFile>
#include <osgGA/GUIEventAdapter>
#include <osgUtil/SceneView>
#include <osgViewer/CompositeViewer>
#include <osgViewer/Renderer>
#include <osgViewer/Viewer>

#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE 0x8861
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST 0x0BC0
#endif
#ifndef GL_VERTEX_PROGRAM_POINT_SIZE
#define GL_VERTEX_PROGRAM_POINT_SIZE 0x8642
#endif

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

// GLSL 120: legal on a Linux legacy/compat context and on 3.3 +
// COMPATIBILITY_PROFILE. Lighting, fog, and point size live here so we
// never emit Material / LightModel / LIGHTING / FOG / POINT_SMOOTH /
// PointSprite / TEXTURE_2D modes (illegal on a CORE profile).
const char* kLitTexVS = R"(
#version 120
varying vec3 vN;
varying vec2 vUV;
varying float vEyeDist;
void main() {
    vN = gl_Normal;
    vUV = gl_MultiTexCoord0.xy;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    gl_Position = ftransform();
}
)";

const char* kLitTexFS = R"(
#version 120
uniform sampler2D sTex;
uniform vec3 uSunDir;
uniform vec3 uAmbient;
uniform vec3 uDiffuse;
uniform vec3 uFogColor;
uniform float uFogDensity;
varying vec3 vN;
varying vec2 vUV;
varying float vEyeDist;
void main() {
    vec3 n = normalize(vN);
    float ndl = max(dot(n, normalize(uSunDir)), 0.0);
    vec3 albedo = texture2D(sTex, vUV).rgb;
    vec3 col = albedo * (uAmbient + uDiffuse * ndl);
    float f = exp(-uFogDensity * uFogDensity * vEyeDist * vEyeDist);
    gl_FragColor = vec4(mix(uFogColor, col, clamp(f, 0.0, 1.0)), 1.0);
}
)";

const char* kLitColorVS = R"(
#version 120
varying vec3 vN;
varying vec4 vCol;
varying float vEyeDist;
void main() {
    vN = gl_Normal;
    vCol = gl_Color;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    gl_Position = ftransform();
}
)";

const char* kLitColorFS = R"(
#version 120
uniform vec3 uSunDir;
uniform vec3 uAmbient;
uniform vec3 uDiffuse;
uniform vec3 uFogColor;
uniform float uFogDensity;
varying vec3 vN;
varying vec4 vCol;
varying float vEyeDist;
void main() {
    vec3 n = normalize(vN);
    float ndl = max(dot(n, normalize(uSunDir)), 0.0);
    vec3 col = vCol.rgb * (uAmbient + uDiffuse * ndl);
    float f = exp(-uFogDensity * uFogDensity * vEyeDist * vEyeDist);
    gl_FragColor = vec4(mix(uFogColor, col, clamp(f, 0.0, 1.0)), vCol.a);
}
)";

const char* kUnlitColorVS = R"(
#version 120
varying vec4 vCol;
varying float vEyeDist;
void main() {
    vCol = gl_Color;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    gl_Position = ftransform();
}
)";

const char* kUnlitColorFS = R"(
#version 120
uniform vec3 uFogColor;
uniform float uFogDensity;
varying vec4 vCol;
varying float vEyeDist;
void main() {
    float f = exp(-uFogDensity * uFogDensity * vEyeDist * vEyeDist);
    gl_FragColor = vec4(mix(uFogColor, vCol.rgb, clamp(f, 0.0, 1.0)), vCol.a);
}
)";

const char* kPointVS = R"(
#version 120
varying vec4 vCol;
varying float vEyeDist;
uniform float uPointSize;
void main() {
    vCol = gl_Color;
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vEyeDist = length(eye.xyz);
    gl_PointSize = uPointSize;
    gl_Position = ftransform();
}
)";

const char* kPointFS = R"(
#version 120
uniform vec3 uFogColor;
uniform float uFogDensity;
varying vec4 vCol;
varying float vEyeDist;
void main() {
    float f = exp(-uFogDensity * uFogDensity * vEyeDist * vEyeDist);
    gl_FragColor = vec4(mix(uFogColor, vCol.rgb, clamp(f, 0.0, 1.0)), vCol.a);
}
)";

void strip_ff(osg::StateSet* ss) {
    if (!ss)
        return;
    ss->removeMode(GL_LIGHTING);
    ss->removeMode(GL_LIGHT0);
    ss->removeMode(GL_FOG);
    ss->removeMode(GL_ALPHA_TEST);
    ss->removeMode(GL_POINT_SMOOTH);
    ss->removeMode(GL_POINT_SPRITE);
    ss->removeMode(GL_TEXTURE_2D);
    ss->removeAttribute(osg::StateAttribute::MATERIAL);
    ss->removeAttribute(osg::StateAttribute::LIGHTMODEL);
    ss->removeAttribute(osg::StateAttribute::LIGHT);
    ss->removeAttribute(osg::StateAttribute::FOG);
    ss->removeAttribute(osg::StateAttribute::POINTSPRITE);
    ss->removeTextureMode(0, GL_TEXTURE_2D);
    ss->removeTextureAttribute(0, osg::StateAttribute::POINTSPRITE);
}

void configure_gl_state(osgViewer::CompositeViewer& viewer, bool check_errors) {
    const auto mode = check_errors ? osg::State::ONCE_PER_ATTRIBUTE
                                   : osg::State::NEVER_CHECK_GL_ERRORS;
    for (unsigned i = 0; i < viewer.getNumViews(); ++i) {
        osgViewer::View* view = viewer.getView(i);
        if (!view)
            continue;
        osg::Camera* cam = view->getCamera();
        if (!cam)
            continue;
        strip_ff(cam->getOrCreateStateSet());
        if (auto* gc = cam->getGraphicsContext()) {
            if (auto* st = gc->getState())
                st->setCheckForGLErrors(mode);
        }
        auto* renderer = dynamic_cast<osgViewer::Renderer*>(cam->getRenderer());
        if (!renderer)
            continue;
        for (int s = 0; s < 2; ++s) {
            if (auto* sv = renderer->getSceneView(s)) {
                strip_ff(sv->getGlobalStateSet());
                strip_ff(sv->getLocalStateSet());
                if (auto* st = sv->getState())
                    st->setCheckForGLErrors(mode);
            }
        }
    }
}

osg::Program* make_program(const char* vs, const char* fs) {
    auto* p = new osg::Program;
    p->addShader(new osg::Shader(osg::Shader::VERTEX, vs));
    p->addShader(new osg::Shader(osg::Shader::FRAGMENT, fs));
    return p;
}

void attach_lit_tex(osg::StateSet* ss, osg::Texture2D* tex) {
    ss->setTextureAttribute(0, tex);
    ss->addUniform(new osg::Uniform("sTex", 0));
    ss->setAttributeAndModes(make_program(kLitTexVS, kLitTexFS), osg::StateAttribute::ON);
    strip_ff(ss);
}

void attach_lit_color(osg::StateSet* ss) {
    ss->setAttributeAndModes(make_program(kLitColorVS, kLitColorFS), osg::StateAttribute::ON);
    strip_ff(ss);
}

void attach_unlit_color(osg::StateSet* ss) {
    ss->setAttributeAndModes(make_program(kUnlitColorVS, kUnlitColorFS), osg::StateAttribute::ON);
    strip_ff(ss);
}

void attach_points(osg::StateSet* ss, float size) {
    ss->setAttributeAndModes(new osg::Point(size));
    ss->addUniform(new osg::Uniform("uPointSize", size));
    ss->setMode(GL_VERTEX_PROGRAM_POINT_SIZE, osg::StateAttribute::ON);
    ss->setAttributeAndModes(make_program(kPointVS, kPointFS), osg::StateAttribute::ON);
    strip_ff(ss);
}

struct ShaderEnv {
    osg::ref_ptr<osg::Uniform> sun_dir;
    osg::ref_ptr<osg::Uniform> ambient;
    osg::ref_ptr<osg::Uniform> diffuse;
    osg::ref_ptr<osg::Uniform> fog_color;
    osg::ref_ptr<osg::Uniform> fog_density;
};

void attach_env_uniforms(osg::StateSet* ss, ShaderEnv& env) {
    env.sun_dir = new osg::Uniform("uSunDir", osg::Vec3(1.f, 0.f, 0.f));
    env.ambient = new osg::Uniform("uAmbient", osg::Vec3(0.15f, 0.15f, 0.18f));
    env.diffuse = new osg::Uniform("uDiffuse", osg::Vec3(1.f, 0.95f, 0.85f));
    env.fog_color = new osg::Uniform("uFogColor", osg::Vec3(0.62f, 0.70f, 0.78f));
    env.fog_density = new osg::Uniform("uFogDensity", 1e-6f);
    ss->addUniform(env.sun_dir);
    ss->addUniform(env.ambient);
    ss->addUniform(env.diffuse);
    ss->addUniform(env.fog_color);
    ss->addUniform(env.fog_density);
    strip_ff(ss);
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

osg::Node* make_procedural_globe(float aniso = 16.f) {
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
    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    tex->setMaxAnisotropy(std::max(1.f, aniso));
    attach_lit_tex(geom->getOrCreateStateSet(), tex);
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

osg::Node* make_dem_patch(const CacheRegion& cache, float aniso = 16.f) {
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
    tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
    tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    tex->setMaxAnisotropy(std::max(1.f, aniso));
    attach_lit_tex(geom->getOrCreateStateSet(), tex);
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
    attach_lit_color(geom->getOrCreateStateSet());
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
    ss->setMode(GL_BLEND, osg::StateAttribute::ON);
    ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE));
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0, 1, false));
    attach_points(ss, 5.0f);
    ss->setRenderBinDetails(20, "DepthSortedBin");
    return g;
}

struct PrecipGpu {
    osg::ref_ptr<osg::Geometry> geom;
    osg::ref_ptr<osg::Vec3Array> verts;
    osg::ref_ptr<osg::Vec4Array> colors;
    std::vector<osg::Vec3> seed;
};

PrecipGpu make_precip_gpu() {
    PrecipGpu g;
    constexpr int n = 360;
    g.geom = new osg::Geometry;
    g.verts = new osg::Vec3Array;
    g.colors = new osg::Vec4Array;
    g.verts->resize(n);
    g.colors->resize(n);
    g.seed.resize(n);
    for (int i = 0; i < n; ++i) {
        g.seed[static_cast<std::size_t>(i)] =
            osg::Vec3(static_cast<float>((i * 37) % 80 - 40), static_cast<float>((i * 19) % 80 - 40),
                      static_cast<float>((i * 13) % 55));
        (*g.verts)[static_cast<unsigned>(i)] = osg::Vec3(0, 0, 0);
        (*g.colors)[static_cast<unsigned>(i)] = osg::Vec4(0, 0, 0, 0);
    }
    g.geom->setVertexArray(g.verts);
    g.geom->setColorArray(g.colors, osg::Array::BIND_PER_VERTEX);
    g.geom->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, n));
    g.geom->setUseDisplayList(false);
    g.geom->setUseVertexBufferObjects(true);
    auto* ss = g.geom->getOrCreateStateSet();
    ss->setMode(GL_BLEND, osg::StateAttribute::ON);
    ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    attach_points(ss, 2.2f);
    ss->setRenderBinDetails(25, "DepthSortedBin");
    return g;
}

float storm_radius_m(const xt4::StormCell& cell) {
    const float k = 0.5f + 0.05f * std::max(0, cell.cfg.precip_intensity);
    return 2778.f * k; // 1.5 nm stand-in; XT4 sized cells from the IG catalog
}

void update_precip(PrecipGpu& g, const HostState& host, float dt) {
    if (!g.verts || g.verts->empty())
        return;
    int ptype = host.have_env ? host.env.precip_type : 0;
    int pint = host.have_env ? host.env.precip_intensity : 0;
    if (host.have_eyepoint) {
        for (const auto& cell : host.storms) {
            if (!cell.cfg.active || !cell.have_pose || cell.cfg.precip_type <= 0)
                continue;
            const double dlat = (host.eye.lat_deg - cell.pose.lat_deg) * 111320.0;
            const double dlon = (host.eye.lon_deg - cell.pose.lon_deg) * 111320.0 *
                                std::cos(geo::deg2rad(host.eye.lat_deg));
            const double dist = std::sqrt(dlat * dlat + dlon * dlon);
            if (dist < storm_radius_m(cell) * 1.2) {
                if (cell.cfg.precip_intensity > pint) {
                    pint = cell.cfg.precip_intensity;
                    ptype = cell.cfg.precip_type;
                }
            }
        }
    }
    const bool on = ptype > 0 && pint > 0 && host.have_eyepoint;
    const float k = on ? std::min(1.f, pint / 10.f) : 0.f;
    if (!on || k <= 0.f) {
        for (unsigned i = 0; i < g.colors->size(); ++i)
            (*g.colors)[i] = osg::Vec4(0, 0, 0, 0);
        g.colors->dirty();
        return;
    }
    const bool snow = ptype == 2;
    const bool hail = ptype == 3;
    const float fall = (snow ? 9.f : hail ? 40.f : 32.f) * dt;
    // Wind FROM dir (deg, 0=N): particles drift with the flow.
    const float wrad = static_cast<float>(geo::deg2rad(host.env.wind_dir_deg));
    const float wspd = host.env.wind_speed_mph * 0.15f;
    const float we = -std::sin(wrad) * wspd;
    const float wn = -std::cos(wrad) * wspd;
    const geo::Ecef eye =
        geo::lla_to_ecef(host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM);
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes(host.eye.lat_deg, host.eye.lon_deg, e, n, u);
    const osg::Vec4 col = snow ? osg::Vec4(0.92f, 0.94f, 0.98f, 0.55f * k)
                               : hail ? osg::Vec4(0.86f, 0.88f, 0.92f, 0.45f * k)
                                      : osg::Vec4(0.70f, 0.76f, 0.84f, 0.35f * k);
    const float psz = snow ? 3.4f : hail ? 3.8f : 2.2f;
    auto* pss = g.geom->getOrCreateStateSet();
    if (auto* pt = dynamic_cast<osg::Point*>(pss->getAttribute(osg::StateAttribute::POINT)))
        pt->setSize(psz);
    if (auto* u = pss->getUniform("uPointSize"))
        u->set(psz);
    for (std::size_t i = 0; i < g.seed.size(); ++i) {
        osg::Vec3& s = g.seed[i];
        s.z() -= fall;
        s.x() += we * dt;
        s.y() += wn * dt;
        if (s.z() < -8.f)
            s.z() += 55.f;
        if (s.x() > 40.f)
            s.x() -= 80.f;
        if (s.x() < -40.f)
            s.x() += 80.f;
        if (s.y() > 40.f)
            s.y() -= 80.f;
        if (s.y() < -40.f)
            s.y() += 80.f;
        const geo::Ecef p{eye.x + e.x * s.x() + n.x * s.y() + u.x * s.z(),
                          eye.y + e.y * s.x() + n.y * s.y() + u.y * s.z(),
                          eye.z + e.z * s.x() + n.z * s.y() + u.z * s.z()};
        (*g.verts)[static_cast<unsigned>(i)] =
            osg::Vec3(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
        (*g.colors)[static_cast<unsigned>(i)] = col;
    }
    g.verts->dirty();
    g.colors->dirty();
    g.geom->dirtyBound();
}

struct MeshGpu {
    osg::ref_ptr<osg::Geometry> geom;
    osg::ref_ptr<osg::Vec3Array> verts;
    osg::ref_ptr<osg::Vec4Array> cols;
    osg::ref_ptr<osg::DrawArrays> draw;
};

MeshGpu make_mesh_gpu(int bin) {
    MeshGpu g;
    g.geom = new osg::Geometry;
    g.verts = new osg::Vec3Array;
    g.cols = new osg::Vec4Array;
    g.draw = new osg::DrawArrays(GL_TRIANGLES, 0, 0);
    g.geom->setVertexArray(g.verts);
    g.geom->setColorArray(g.cols, osg::Array::BIND_PER_VERTEX);
    g.geom->addPrimitiveSet(g.draw);
    g.geom->setUseDisplayList(false);
    g.geom->setUseVertexBufferObjects(true);
    auto* ss = g.geom->getOrCreateStateSet();
    ss->setMode(GL_BLEND, osg::StateAttribute::ON);
    ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0, 1, false));
    attach_unlit_color(ss);
    ss->setRenderBinDetails(bin, "DepthSortedBin");
    return g;
}

void mesh_begin(MeshGpu& g) {
    g.verts->clear();
    g.cols->clear();
}

void mesh_end(MeshGpu& g) {
    g.draw->setCount(static_cast<GLint>(g.verts->size()));
    g.verts->dirty();
    g.cols->dirty();
    g.geom->dirtyBound();
}

void add_tri(MeshGpu& g, const osg::Vec3& a, const osg::Vec3& b, const osg::Vec3& c,
             const osg::Vec4& col) {
    g.verts->push_back(a);
    g.verts->push_back(b);
    g.verts->push_back(c);
    g.cols->push_back(col);
    g.cols->push_back(col);
    g.cols->push_back(col);
}

osg::Vec3 enu_point(const geo::Ecef& o, const geo::Ecef& e, const geo::Ecef& n, const geo::Ecef& u,
                    double east, double north, double up) {
    return osg::Vec3(static_cast<float>(o.x + e.x * east + n.x * north + u.x * up),
                     static_cast<float>(o.y + e.y * east + n.y * north + u.y * up),
                     static_cast<float>(o.z + e.z * east + n.z * north + u.z * up));
}

void add_disc(MeshGpu& g, double lat, double lon, double alt_m, float radius_m, int nseg,
              const osg::Vec4& col) {
    const geo::Ecef o = geo::lla_to_ecef(lat, lon, alt_m);
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes(lat, lon, e, n, u);
    const osg::Vec3 ctr = enu_point(o, e, n, u, 0, 0, 0);
    constexpr float kPi = 3.14159265f;
    for (int i = 0; i < nseg; ++i) {
        const float a0 = 2.f * kPi * static_cast<float>(i) / nseg;
        const float a1 = 2.f * kPi * static_cast<float>(i + 1) / nseg;
        add_tri(g, ctr,
                enu_point(o, e, n, u, radius_m * std::cos(a0), radius_m * std::sin(a0), 0),
                enu_point(o, e, n, u, radius_m * std::cos(a1), radius_m * std::sin(a1), 0), col);
    }
}

void add_card(MeshGpu& g, double lat, double lon, double alt0_m, double alt1_m, float east,
              float north, float half_w, const osg::Vec4& col) {
    const geo::Ecef o = geo::lla_to_ecef(lat, lon, 0);
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes(lat, lon, e, n, u);
    // Tangent to the ring so the card faces the eyepoint.
    float len = std::sqrt(east * east + north * north);
    if (len < 1.f)
        len = 1.f;
    const float te = -north / len * half_w;
    const float tn = east / len * half_w;
    const osg::Vec3 a = enu_point(o, e, n, u, east + te, north + tn, alt0_m);
    const osg::Vec3 b = enu_point(o, e, n, u, east - te, north - tn, alt0_m);
    const osg::Vec3 c = enu_point(o, e, n, u, east - te, north - tn, alt1_m);
    const osg::Vec3 d = enu_point(o, e, n, u, east + te, north + tn, alt1_m);
    add_tri(g, a, b, c, col);
    add_tri(g, a, c, d, col);
}

void layer_alts(const xt4::CloudLayer& L, int which, float& lo, float& hi) {
    lo = L.ceiling_ft;
    hi = L.top_ft;
    if (which == 2 && L.on() && hi <= lo) {
        lo = 25000.f;
        hi = 28000.f;
    }
}

bool eye_in_layer(const HostState& host, const xt4::CloudLayer& L, int which) {
    if (!L.on() || !host.have_eyepoint)
        return false;
    float lo = 0, hi = 0;
    layer_alts(L, which, lo, hi);
    return host.eye.alt_ft >= lo && host.eye.alt_ft <= hi && hi > lo;
}

void update_clouds(MeshGpu& g, const HostState& host, float drift_m) {
    mesh_begin(g);
    if (!host.have_cloud || !host.have_eyepoint) {
        mesh_end(g);
        return;
    }
    const double lat = host.eye.lat_deg;
    const double lon = host.eye.lon_deg;
    const osg::Vec4 cols[3] = {
        osg::Vec4(0.52f, 0.55f, 0.60f, 0.30f),
        osg::Vec4(0.68f, 0.71f, 0.76f, 0.22f),
        osg::Vec4(0.84f, 0.86f, 0.90f, 0.12f),
    };
    for (int li = 0; li < 3; ++li) {
        const auto& L = host.clouds[static_cast<std::size_t>(li)];
        if (!L.on())
            continue;
        float lo_ft = 0, hi_ft = 0;
        layer_alts(L, li, lo_ft, hi_ft);
        if (hi_ft <= lo_ft)
            continue;
        const float lo_m = lo_ft * static_cast<float>(geo::kFtToM);
        const float hi_m = hi_ft * static_cast<float>(geo::kFtToM);
        const float mid_m = 0.5f * (lo_m + hi_m);
        float rad = 9000.f;
        if (L.scud & 1)
            rad += 600.f;
        const float we =
            (L.wind_kt != 0.f ? L.wind_kt * 0.514f : host.env.wind_speed_mph * 0.447f);
        const float wrad = static_cast<float>(
            geo::deg2rad(L.wind_kt != 0.f ? L.wind_dir_deg : host.env.wind_dir_deg));
        const float ox = std::fmod(drift_m * -std::sin(wrad) * we * 0.04f, 4000.f);
        const float oy = std::fmod(drift_m * -std::cos(wrad) * we * 0.04f, 4000.f);
        const double clat = lat + oy / 111320.0;
        const double clon = lon + ox / (111320.0 * std::max(0.2, std::cos(geo::deg2rad(lat))));
        osg::Vec4 col = cols[li];
        if (eye_in_layer(host, L, li))
            col.a() *= 0.55f;
        add_disc(g, clat, clon, lo_m, rad, 14, col);
        add_disc(g, clat, clon, mid_m, rad * 0.92f, 12, col);
        add_disc(g, clat, clon, hi_m, rad * 0.85f, 12, col);
        constexpr int kCards = 8;
        for (int i = 0; i < kCards; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kCards;
            const float r = rad * 0.55f;
            add_card(g, clat, clon, lo_m, hi_m, r * std::cos(a), r * std::sin(a), 1400.f,
                     osg::Vec4(col.r(), col.g(), col.b(), col.a() * 0.7f));
        }
    }
    mesh_end(g);
}

void update_storms(MeshGpu& g, const HostState& host, float flash) {
    mesh_begin(g);
    for (const auto& cell : host.storms) {
        if (!cell.cfg.active || !cell.have_pose)
            continue;
        const double lat = cell.pose.lat_deg;
        const double lon = cell.pose.lon_deg;
        const float rad = storm_radius_m(cell);
        const float base_m = static_cast<float>(std::max(200.0, cell.pose.alt_ft * geo::kFtToM - 800.0));
        const float top_m = base_m + 7000.f;
        osg::Vec4 col(0.16f + 0.45f * flash, 0.17f + 0.45f * flash, 0.20f + 0.50f * flash,
                      0.42f + 0.25f * flash);
        add_disc(g, lat, lon, base_m, rad, 12, col);
        add_disc(g, lat, lon, 0.4f * base_m + 0.6f * top_m, rad * 0.75f, 10, col);
        add_disc(g, lat, lon, top_m, rad * 0.45f, 10, col);
        for (int i = 0; i < 6; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 6.f;
            add_card(g, lat, lon, base_m, top_m, rad * 0.4f * std::cos(a),
                     rad * 0.4f * std::sin(a), rad * 0.35f, col);
        }
    }
    mesh_end(g);
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
        viewer_->setKeyEventSetsDone(osgGA::GUIEventAdapter::KEY_Escape);
#ifdef __linux__
        const bool linux_host = true;
#else
        const bool linux_host = false;
#endif
        const GlDisplayPlan plan = plan_gl_display(linux_host, cfg.render.gl_debug);
        gl_check_errors_ = plan.check_errors;
        if (plan.check_errors)
            osg::setNotifyLevel(osg::INFO);
        // Linux: leave the version unset so NVIDIA hands back a 4.6
        // compatibility (legacy) context. setGLContextVersion("3.3") with
        // no profile mask is CORE on NVIDIA 595, and apt OSG's FF path
        // then floods "invalid enumerant". Elsewhere request 3.3 +
        // COMPATIBILITY_PROFILE_BIT. Scene StateSets use shaders either way.
        if (osg::ref_ptr<osg::DisplaySettings> ds = osg::DisplaySettings::instance()) {
            if (!plan.version.empty()) {
                ds->setGLContextVersion(plan.version);
                if (plan.profile_mask != 0)
                    ds->setGLContextProfileMask(plan.profile_mask);
            }
            if (cfg.render.msaa > 0)
                ds->setNumMultiSamples(std::max(0, std::min(cfg.render.msaa, 8)));
        }

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
            root->addChild(make_procedural_globe(static_cast<float>(cfg.render.aniso)));
        }
#else
        root->addChild(make_procedural_globe(static_cast<float>(cfg.render.aniso)));
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
                        if (auto* patch = make_dem_patch(cache, static_cast<float>(cfg.render.aniso))) {
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
                if (auto* patch = make_dem_patch(cache, static_cast<float>(cfg.render.aniso))) {
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
            precip_gpu_ = make_precip_gpu();
            if (precip_gpu_.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(precip_gpu_.geom);
                root->addChild(geode);
            }
            cloud_gpu_ = make_mesh_gpu(12);
            storm_gpu_ = make_mesh_gpu(13);
            if (cloud_gpu_.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(cloud_gpu_.geom);
                root->addChild(geode);
            }
            if (storm_gpu_.geom) {
                auto* geode = new osg::Geode;
                geode->addDrawable(storm_gpu_.geom);
                root->addChild(geode);
            }
        }
        attach_env_uniforms(root->getOrCreateStateSet(), env_u_);
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
        configure_gl_state(*viewer_, gl_check_errors_);
        S376_LOG_INFO("render",
                      "opened %zu channel window(s)  screens=%d  %dx%d  gl %s%s%s",
                      views_.size(), nscr, sw, sh,
                      plan.version.empty() ? "legacy-compat" : plan.version.c_str(),
                      plan.profile_mask ? " +compat-mask" : "",
                      plan.check_errors ? "  gl-debug" : "");
        return true;
    }

    void frame(const HostState& host) override {
        if (!viewer_)
            return;
        if (!stripped_ff_) {
            configure_gl_state(*viewer_, gl_check_errors_);
            stripped_ff_ = true;
        }
        for (std::size_t i = 0; i < views_.size() && i < channels_.size(); ++i)
            apply_view_camera(views_[i]->getCamera(), channels_[i], host);
        apply_env(host);
        apply_light_steps(lights_gpu_, host);
        const float dt = 1.f / 60.f;
        cloud_drift_m_ += 8.f * dt;
        update_precip(precip_gpu_, host, dt);
        update_clouds(cloud_gpu_, host, cloud_drift_m_);
        update_storms(storm_gpu_, host, storm_flash_);
        viewer_->frame();
    }

    bool done() const override { return viewer_ && viewer_->done(); }
    void request_stop() override {
        if (viewer_)
            viewer_->setDone(true);
    }
    void end() override {
        if (viewer_)
            viewer_->setDone(true);
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
        bool in_cloud = false;
        if (host.have_cloud && host.have_eyepoint) {
            for (int i = 0; i < 3; ++i) {
                const auto& L = host.clouds[static_cast<std::size_t>(i)];
                if (!L.on())
                    continue;
                float lo = 0, hi = 0;
                layer_alts(L, i, lo, hi);
                if (host.eye.alt_ft > hi && L.vis_above_ft > 0)
                    vis_ft = std::min(vis_ft, L.vis_above_ft);
                if (host.eye.alt_ft >= lo && host.eye.alt_ft <= hi && hi > lo)
                    in_cloud = true;
            }
        }
        vis_ft = std::max(200.f, std::min(vis_ft, xt4::kMaxVisibilityFt));
        const float vis_m = vis_ft * static_cast<float>(geo::kFtToM);
        float dens = 1.7f / vis_m;
        if (host.have_env && host.env.precip_type > 0)
            dens *= 1.f + 0.12f * std::min(10, host.env.precip_intensity);
        // Fog height is AGL. Use last HAT sample so we do not treat MSL as AGL.
        if (host.have_fog && host.fog.select >= 0 && host.have_hat &&
            host.hat_ft < host.fog.height_ft + 200.f)
            dens *= 1.35f;
        if (in_cloud)
            dens *= 2.4f;
        if (env_u_.fog_density)
            env_u_.fog_density->set(dens);
        if (env_u_.fog_color)
            env_u_.fog_color->set(osg::Vec3(0.62f, 0.70f, 0.78f));

        float tod = host.have_env ? host.env.tod_hours : 14.5f;
        // Type-124 tod_select: −1 = continuous hours; 0 day / 1 dusk / 2 dawn / 3 night.
        if (host.have_env && host.env.tod_select >= 0) {
            switch (host.env.tod_select) {
            case 0:
                tod = 14.5f;
                break;
            case 1:
                tod = 19.0f;
                break;
            case 2:
                tod = 6.0f;
                break;
            case 3:
                tod = 0.5f;
                break;
            default:
                break;
            }
        }
        const int month = host.have_env ? host.env.month : 6;
        const int day = host.have_env ? host.env.day : 21;
        const double lat = host.have_eyepoint ? host.eye.lat_deg : 47.46;
        const double lon = host.have_eyepoint ? host.eye.lon_deg : -122.31;
        double el = 0;
        const geo::Ecef s = geo::sun_ecef(lat, lon, tod, month, day, &el);
        if (env_u_.sun_dir)
            env_u_.sun_dir->set(osg::Vec3(static_cast<float>(s.x), static_cast<float>(s.y),
                                          static_cast<float>(s.z)));
        const float dayf = static_cast<float>(std::max(0.0, std::sin(geo::deg2rad(el))));
        float flash = 0.f;
        if (host.have_env && host.env.lightning_trigger != 0) {
            if (host.env.lightning_trigger != last_lightning_)
                flash_left_ = 4;
            last_lightning_ = host.env.lightning_trigger;
        }
        for (int i = 0; i < xt4::kMaxActiveStorms; ++i) {
            const auto& cell = host.storms[static_cast<std::size_t>(i)];
            if (!cell.cfg.active)
                continue;
            if (cell.cfg.lightning > 1 && cell.cfg.lightning != last_storm_bolt_[i]) {
                flash_left_ = 5;
                storm_flash_left_ = 5;
            }
            last_storm_bolt_[i] = cell.cfg.lightning;
            if (cell.cfg.lightning == 1 && (storm_tick_ + i * 17) % 97 == 0) {
                flash_left_ = 3;
                storm_flash_left_ = 3;
            }
        }
        ++storm_tick_;
        if (flash_left_ > 0) {
            flash = 0.65f;
            --flash_left_;
        }
        storm_flash_ = 0.f;
        if (storm_flash_left_ > 0) {
            storm_flash_ = 0.85f;
            --storm_flash_left_;
        }
        const float dk = 0.15f + 0.85f * dayf + flash;
        if (env_u_.diffuse)
            env_u_.diffuse->set(osg::Vec3(1.0f * dk, 0.95f * dk, 0.85f * dk));
        if (env_u_.ambient)
            env_u_.ambient->set(osg::Vec3(0.04f + 0.12f * dayf + flash * 0.4f,
                                          0.04f + 0.12f * dayf + flash * 0.4f,
                                          0.06f + 0.12f * dayf + flash * 0.5f));
        const float t = 0.25f + 0.55f * dayf;
        const osg::Vec4 sky(0.15f + 0.25f * t + flash * 0.4f, 0.22f + 0.35f * t + flash * 0.4f,
                            0.35f + 0.40f * t + flash * 0.3f, 1);
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
    ShaderEnv env_u_;
    LightGpu lights_gpu_;
    PrecipGpu precip_gpu_;
    MeshGpu cloud_gpu_;
    MeshGpu storm_gpu_;
    int last_lightning_ = 0;
    int flash_left_ = 0;
    int last_storm_bolt_[xt4::kMaxActiveStorms] = {};
    int storm_flash_left_ = 0;
    int storm_tick_ = 0;
    float storm_flash_ = 0.f;
    float cloud_drift_m_ = 0.f;
    bool used_earth_ = false;
    bool stripped_ff_ = false;
    bool gl_check_errors_ = false;
};

} // namespace

std::unique_ptr<Renderer> make_osg_renderer() {
    return std::make_unique<OsgRenderer>();
}

} // namespace s376

#endif // S376_HAS_OSG
