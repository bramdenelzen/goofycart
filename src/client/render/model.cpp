#include "model.h"


#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace gk::render {
namespace {

namespace fs = std::filesystem;

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Finds a texture file named in a model: as given, relative to the model's
// folder, by basename next to the model, with a .png extension instead, or
// anywhere under the model's folder (rips keep textures in subfolders).
std::string resolve_texture(const std::string& name_in, const fs::path& model_dir) {
    std::string name = name_in;
    if (name.rfind("file://", 0) == 0) name = name.substr(7);
    for (char& c : name)
        if (c == '\\') c = '/';
    // Strip URL escapes such as %20.
    std::string un;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '%' && i + 2 < name.size()) {
            un += char(std::strtol(name.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            un += name[i];
        }
    }
    name = un;
    if (name.empty()) return {};
    std::error_code ec;
    const fs::path given(name);
    const fs::path base = given.filename();
    std::vector<fs::path> candidates{given, model_dir / given, model_dir / base, (model_dir / base).replace_extension(".png"),
                                     (model_dir / base).replace_extension(".dds"), model_dir / "textures" / base,
                                     (model_dir / "textures" / base).replace_extension(".png"),
                                     (model_dir / "textures" / base).replace_extension(".dds")};
    for (const fs::path& c : candidates)
        if (fs::is_regular_file(c, ec)) return c.string();
    // Last resort: the same basename (any extension stb reads) anywhere below.
    const std::string stem = lower(base.stem().string());
    if (stem.empty()) return {};
    for (const auto& entry : fs::recursive_directory_iterator(model_dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = lower(entry.path().extension().string());
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".tga" && ext != ".bmp" && ext != ".dds") continue;
        if (lower(entry.path().stem().string()) == stem) return entry.path().string();
    }
    return {};
}

void finish(ModelData& m) {
    m.bounds_min = vec3(1e30f);
    m.bounds_max = vec3(-1e30f);
    bool any = false;
    for (const auto& p : m.parts)
        for (const auto& v : p.mesh.vertices) {
            m.bounds_min = glm::min(m.bounds_min, v.pos);
            m.bounds_max = glm::max(m.bounds_max, v.pos);
            any = true;
        }
    if (!any) m.bounds_min = m.bounds_max = vec3(0.0f);
}

// Accumulates triangles per texture, with flat normals where the file has none.
struct PartSink {
    std::map<std::string, size_t> index; // texture|material -> part
    ModelData* out;
    ModelPart& part(const std::string& texture, const std::string& material, bool two_sided) {
        const std::string key = texture + "|" + material;
        auto it = index.find(key);
        if (it == index.end()) {
            out->parts.push_back({{}, texture, material, two_sided});
            it = index.emplace(key, out->parts.size() - 1).first;
        }
        return out->parts[it->second];
    }
    static void triangle(ModelPart& p, const vec3* pos, const vec3* nrm, const vec2* uv, bool has_normals) {
        vec3 flat = glm::cross(pos[1] - pos[0], pos[2] - pos[0]);
        const float len = glm::length(flat);
        if (len < 1e-14f) return;
        flat /= len;
        const auto base = static_cast<uint32_t>(p.mesh.vertices.size());
        for (int i = 0; i < 3; ++i) {
            vec3 n = has_normals ? nrm[i] : flat;
            if (glm::length(n) < 1e-6f) n = flat;
            else n = glm::normalize(n);
            p.mesh.vertices.push_back({pos[i], n, rgba(255, 255, 255), vec2(uv[i].x, 1.0f - uv[i].y), 0});
        }
        p.mesh.indices.insert(p.mesh.indices.end(), {base, base + 1, base + 2});
    }
};

// ---- OBJ

std::optional<ModelData> load_obj(const std::string& path, const std::string& text, std::string* error) {
    const fs::path dir = fs::path(path).parent_path();
    std::vector<vec3> positions, normals;
    std::vector<vec2> uvs;
    std::unordered_map<std::string, std::string> material_texture; // material -> texture path
    ModelData model;
    PartSink sink{{}, &model};
    std::string material;

    auto load_mtl = [&](const std::string& name) {
        const auto mtl = read_file((dir / name).string());
        if (!mtl) return;
        std::istringstream in(*mtl);
        std::string line, current;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string key;
            ls >> key;
            if (key == "newmtl") ls >> current;
            else if (key == "map_Kd" && !current.empty()) {
                // The path is the rest of the line, after any options.
                std::string rest;
                std::getline(ls, rest);
                size_t s = rest.find_first_not_of(" \t");
                if (s == std::string::npos) continue;
                rest = rest.substr(s);
                while (rest.rfind("-", 0) == 0) { // -s 1 1 1 etc.
                    const size_t sp = rest.find(' ');
                    if (sp == std::string::npos) break;
                    rest = rest.substr(sp + 1);
                    // skip the option's values
                    while (!rest.empty() && (std::isdigit(static_cast<unsigned char>(rest[0])) || rest[0] == '.' || rest[0] == '-')) {
                        const size_t sp2 = rest.find(' ');
                        if (sp2 == std::string::npos) break;
                        rest = rest.substr(sp2 + 1);
                    }
                }
                while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) rest.pop_back();
                material_texture[current] = rest;
            }
        }
    };

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "v") {
            vec3 p;
            ls >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (key == "vn") {
            vec3 n;
            ls >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (key == "vt") {
            vec2 t;
            ls >> t.x >> t.y;
            uvs.push_back(t);
        } else if (key == "mtllib") {
            std::string rest;
            std::getline(ls, rest);
            const size_t s = rest.find_first_not_of(" \t");
            if (s != std::string::npos) load_mtl(rest.substr(s));
        } else if (key == "usemtl") {
            ls >> material;
        } else if (key == "f") {
            struct Corner { int p = -1, t = -1, n = -1; };
            std::vector<Corner> corners;
            std::string tok;
            while (ls >> tok) {
                Corner c;
                int* slot[3] = {&c.p, &c.t, &c.n};
                size_t start = 0;
                for (int i = 0; i < 3 && start <= tok.size(); ++i) {
                    const size_t slash = tok.find('/', start);
                    const std::string field = tok.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                    if (!field.empty()) {
                        int idx = std::atoi(field.c_str());
                        const int count = i == 0 ? int(positions.size()) : i == 1 ? int(uvs.size()) : int(normals.size());
                        if (idx < 0) idx = count + idx; else idx -= 1;
                        *slot[i] = idx;
                    }
                    if (slash == std::string::npos) break;
                    start = slash + 1;
                }
                corners.push_back(c);
            }
            if (corners.size() < 3) continue;
            const auto tex_it = material_texture.find(material);
            const std::string texture = tex_it == material_texture.end() ? "" : resolve_texture(tex_it->second, dir);
            ModelPart& part = sink.part(texture, material, false);
            for (size_t i = 1; i + 1 < corners.size(); ++i) {
                const Corner tri[3] = {corners[0], corners[i], corners[i + 1]};
                vec3 pos[3], nrm[3];
                vec2 uv[3];
                bool ok = true, has_n = true;
                for (int k = 0; k < 3; ++k) {
                    if (tri[k].p < 0 || tri[k].p >= int(positions.size())) { ok = false; break; }
                    pos[k] = positions[size_t(tri[k].p)];
                    uv[k] = tri[k].t >= 0 && tri[k].t < int(uvs.size()) ? uvs[size_t(tri[k].t)] : vec2(0.0f);
                    if (tri[k].n >= 0 && tri[k].n < int(normals.size())) nrm[k] = normals[size_t(tri[k].n)];
                    else has_n = false;
                }
                if (ok) PartSink::triangle(part, pos, nrm, uv, has_n);
            }
        }
    }
    if (model.parts.empty()) {
        if (error) *error = "no faces in " + path;
        return std::nullopt;
    }
    finish(model);
    return model;
}

// ---- a small XML reader, enough for COLLADA

struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text;
    std::vector<XmlNode> children;

    const char* attr(const char* key) const {
        for (const auto& [k, v] : attrs)
            if (k == key) return v.c_str();
        return nullptr;
    }
    std::string attr_or(const char* key, const char* def = "") const {
        const char* v = attr(key);
        return v ? v : def;
    }
    const XmlNode* child(const char* name_) const {
        for (const auto& c : children)
            if (c.name == name_) return &c;
        return nullptr;
    }
    // Depth-first search for the first descendant with this name.
    const XmlNode* find(const char* name_) const {
        for (const auto& c : children) {
            if (c.name == name_) return &c;
            if (const XmlNode* d = c.find(name_)) return d;
        }
        return nullptr;
    }
    void find_all(const char* name_, std::vector<const XmlNode*>& out) const {
        for (const auto& c : children) {
            if (c.name == name_) out.push_back(&c);
            c.find_all(name_, out);
        }
    }
};

struct XmlParser {
    std::string_view s;
    size_t i = 0;

    bool starts(const char* lit) const { return s.substr(i).rfind(lit, 0) == 0; }
    void skip_ws() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    static std::string unescape(std::string_view t) {
        if (t.find('&') == std::string_view::npos) return std::string(t);
        std::string out;
        out.reserve(t.size());
        for (size_t k = 0; k < t.size(); ++k) {
            if (t[k] != '&') { out += t[k]; continue; }
            const size_t semi = t.find(';', k);
            if (semi == std::string_view::npos) { out += t[k]; continue; }
            const std::string_view ent = t.substr(k + 1, semi - k - 1);
            if (ent == "amp") out += '&';
            else if (ent == "lt") out += '<';
            else if (ent == "gt") out += '>';
            else if (ent == "quot") out += '"';
            else if (ent == "apos") out += '\'';
            else out += std::string(t.substr(k, semi - k + 1));
            k = semi;
        }
        return out;
    }
    bool skip_misc() { // prolog, comments, doctype, processing instructions
        for (;;) {
            skip_ws();
            if (starts("<?")) {
                const size_t e = s.find("?>", i);
                if (e == std::string_view::npos) return false;
                i = e + 2;
            } else if (starts("<!--")) {
                const size_t e = s.find("-->", i);
                if (e == std::string_view::npos) return false;
                i = e + 3;
            } else if (starts("<!")) {
                const size_t e = s.find('>', i);
                if (e == std::string_view::npos) return false;
                i = e + 1;
            } else {
                return true;
            }
        }
    }
    bool parse_element(XmlNode& node) {
        if (i >= s.size() || s[i] != '<') return false;
        ++i;
        const size_t name_start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') ++i;
        node.name = std::string(s.substr(name_start, i - name_start));
        // attributes
        for (;;) {
            skip_ws();
            if (i >= s.size()) return false;
            if (s[i] == '/') {
                i += 2; // "/>"
                return true;
            }
            if (s[i] == '>') {
                ++i;
                break;
            }
            const size_t ks = i;
            while (i < s.size() && s[i] != '=' && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            const std::string key(s.substr(ks, i - ks));
            skip_ws();
            if (i >= s.size() || s[i] != '=') return false;
            ++i;
            skip_ws();
            if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) return false;
            const char q = s[i++];
            const size_t vs = i;
            const size_t ve = s.find(q, i);
            if (ve == std::string_view::npos) return false;
            node.attrs.emplace_back(key, unescape(s.substr(vs, ve - vs)));
            i = ve + 1;
        }
        // content
        for (;;) {
            const size_t lt = s.find('<', i);
            if (lt == std::string_view::npos) return false;
            if (lt > i) node.text += unescape(s.substr(i, lt - i));
            i = lt;
            if (starts("</")) {
                const size_t e = s.find('>', i);
                if (e == std::string_view::npos) return false;
                i = e + 1;
                return true;
            }
            if (starts("<!--")) {
                const size_t e = s.find("-->", i);
                if (e == std::string_view::npos) return false;
                i = e + 3;
                continue;
            }
            if (starts("<![CDATA[")) {
                const size_t e = s.find("]]>", i);
                if (e == std::string_view::npos) return false;
                node.text += std::string(s.substr(i + 9, e - i - 9));
                i = e + 3;
                continue;
            }
            if (starts("<?")) {
                const size_t e = s.find("?>", i);
                if (e == std::string_view::npos) return false;
                i = e + 2;
                continue;
            }
            node.children.emplace_back();
            if (!parse_element(node.children.back())) return false;
        }
    }
};

bool parse_xml(const std::string& text, XmlNode& root) {
    XmlParser p{text, 0};
    if (!p.skip_misc()) return false;
    return p.parse_element(root);
}

// Fast-ish float list reading for the big arrays.
std::vector<float> floats_of(const std::string& text) {
    std::vector<float> out;
    out.reserve(text.size() / 8);
    const char* c = text.c_str();
    char* end = nullptr;
    for (;;) {
        while (*c && std::isspace(static_cast<unsigned char>(*c))) ++c;
        if (!*c) break;
        const float v = std::strtof(c, &end);
        if (end == c) break;
        out.push_back(v);
        c = end;
    }
    return out;
}
std::vector<int> ints_of(const std::string& text) {
    std::vector<int> out;
    out.reserve(text.size() / 3);
    const char* c = text.c_str();
    char* end = nullptr;
    for (;;) {
        while (*c && std::isspace(static_cast<unsigned char>(*c))) ++c;
        if (!*c) break;
        const long v = std::strtol(c, &end, 10);
        if (end == c) break;
        out.push_back(int(v));
        c = end;
    }
    return out;
}

std::string strip_hash(std::string s) {
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    return s;
}

// ---- COLLADA

struct DaeSource {
    std::vector<float> data;
    int stride = 3;
};

struct DaeContext {
    const XmlNode* root = nullptr;
    fs::path dir;
    std::unordered_map<std::string, const XmlNode*> by_id; // every element with an id
    std::unordered_map<std::string, std::string> image_path;      // image id -> file
    std::unordered_map<std::string, std::string> material_texture; // material id -> resolved texture
    std::unordered_map<std::string, std::string> material_name;    // material id -> name
    std::unordered_map<std::string, bool> material_two_sided;
    mat4 axis = mat4(1.0f);
    float unit = 1.0f;
    ModelData model;
    PartSink sink{{}, nullptr};

    void index(const XmlNode& n) {
        if (const char* id = n.attr("id")) by_id[id] = &n;
        for (const auto& c : n.children) index(c);
    }
    const XmlNode* get(const std::string& ref) const {
        auto it = by_id.find(strip_hash(ref));
        return it == by_id.end() ? nullptr : it->second;
    }
};

std::string trim(std::string s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Resolves an effect's diffuse texture to an image file, following the
// sampler -> surface -> image chain or a direct image reference.
std::string effect_texture(DaeContext& ctx, const XmlNode& effect) {
    std::vector<const XmlNode*> newparams;
    effect.find_all("newparam", newparams);
    auto param = [&](const std::string& sid) -> const XmlNode* {
        for (const XmlNode* p : newparams)
            if (p->attr_or("sid") == sid) return p;
        return nullptr;
    };
    auto image_file = [&](const std::string& image_id) -> std::string {
        auto it = ctx.image_path.find(image_id);
        return it == ctx.image_path.end() ? "" : it->second;
    };
    std::function<std::string(const std::string&, int)> resolve = [&](const std::string& ref, int depth) -> std::string {
        if (depth > 6) return "";
        if (const std::string f = image_file(ref); !f.empty()) return f;
        if (const XmlNode* p = param(ref)) {
            if (const XmlNode* sampler = p->child("sampler2D")) {
                if (const XmlNode* src = sampler->child("source")) return resolve(trim(src->text), depth + 1);
                if (const XmlNode* init = sampler->find("init_from")) return resolve(trim(init->text), depth + 1);
            }
            if (const XmlNode* surface = p->child("surface"))
                if (const XmlNode* init = surface->child("init_from")) return resolve(trim(init->text), depth + 1);
        }
        return "";
    };
    // Prefer the diffuse slot.
    if (const XmlNode* diffuse = effect.find("diffuse"))
        if (const XmlNode* tex = diffuse->child("texture"))
            if (const std::string f = resolve(tex->attr_or("texture"), 0); !f.empty()) return f;
    std::vector<const XmlNode*> textures;
    effect.find_all("texture", textures);
    for (const XmlNode* t : textures)
        if (const std::string f = resolve(t->attr_or("texture"), 0); !f.empty()) return f;
    std::vector<const XmlNode*> inits;
    effect.find_all("init_from", inits);
    for (const XmlNode* n : inits)
        if (const std::string f = image_file(trim(n->text)); !f.empty()) return f;
    return "";
}

mat4 node_transform(const XmlNode& node) {
    mat4 m(1.0f);
    for (const auto& c : node.children) {
        if (c.name == "matrix") {
            const auto v = floats_of(c.text);
            if (v.size() == 16) {
                mat4 t;
                for (int r = 0; r < 4; ++r)
                    for (int col = 0; col < 4; ++col) t[col][r] = v[size_t(r * 4 + col)]; // row-major in the file
                m = m * t;
            }
        } else if (c.name == "translate") {
            const auto v = floats_of(c.text);
            if (v.size() == 3) m = glm::translate(m, vec3(v[0], v[1], v[2]));
        } else if (c.name == "rotate") {
            const auto v = floats_of(c.text);
            if (v.size() == 4 && glm::length(vec3(v[0], v[1], v[2])) > 1e-6f)
                m = glm::rotate(m, glm::radians(v[3]), glm::normalize(vec3(v[0], v[1], v[2])));
        } else if (c.name == "scale") {
            const auto v = floats_of(c.text);
            if (v.size() == 3) m = glm::scale(m, vec3(v[0], v[1], v[2]));
        }
    }
    return m;
}

void add_geometry(DaeContext& ctx, const XmlNode& geometry, const mat4& world,
                  const std::unordered_map<std::string, std::string>& symbol_to_material) {
    const XmlNode* mesh = geometry.child("mesh");
    if (!mesh) return;
    // Sources by id.
    std::unordered_map<std::string, DaeSource> sources;
    for (const auto& c : mesh->children) {
        if (c.name != "source") continue;
        DaeSource s;
        if (const XmlNode* fa = c.child("float_array")) s.data = floats_of(fa->text);
        if (const XmlNode* acc = c.find("accessor")) s.stride = std::max(1, std::atoi(acc->attr_or("stride", "3").c_str()));
        sources[c.attr_or("id")] = std::move(s);
    }
    // <vertices>: POSITION (and sometimes NORMAL) sources by vertices id.
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> vertices; // id -> (semantic, source)
    for (const auto& c : mesh->children) {
        if (c.name != "vertices") continue;
        auto& v = vertices[c.attr_or("id")];
        for (const auto& in : c.children)
            if (in.name == "input") v.emplace_back(in.attr_or("semantic"), strip_hash(in.attr_or("source")));
    }
    const glm::mat3 normal_m = glm::transpose(glm::inverse(glm::mat3(world)));
    const bool mirrored = glm::determinant(glm::mat3(world)) < 0.0f;

    for (const auto& prim : mesh->children) {
        if (prim.name != "triangles" && prim.name != "polylist" && prim.name != "polygons") continue;
        struct Input { std::string semantic; const DaeSource* source = nullptr; int offset = 0; int set = 0; };
        std::vector<Input> inputs;
        int stride = 1;
        for (const auto& in : prim.children) {
            if (in.name != "input") continue;
            const int offset = std::atoi(in.attr_or("offset", "0").c_str());
            stride = std::max(stride, offset + 1);
            const std::string semantic = in.attr_or("semantic");
            const std::string src = strip_hash(in.attr_or("source"));
            const int set = std::atoi(in.attr_or("set", "0").c_str());
            if (semantic == "VERTEX") {
                auto vit = vertices.find(src);
                if (vit == vertices.end()) continue;
                for (const auto& [sem, s] : vit->second) {
                    auto sit = sources.find(s);
                    if (sit != sources.end()) inputs.push_back({sem, &sit->second, offset, set});
                }
            } else {
                auto sit = sources.find(src);
                if (sit != sources.end()) inputs.push_back({semantic, &sit->second, offset, set});
            }
        }
        const Input* pos_in = nullptr;
        const Input* nrm_in = nullptr;
        const Input* uv_in = nullptr;
        for (const auto& in : inputs) {
            if (in.semantic == "POSITION" && !pos_in) pos_in = &in;
            if (in.semantic == "NORMAL" && !nrm_in) nrm_in = &in;
            if (in.semantic == "TEXCOORD" && (!uv_in || in.set < uv_in->set)) uv_in = &in;
        }
        if (!pos_in) continue;

        // Material for this primitive.
        const std::string symbol = prim.attr_or("material");
        std::string material_id;
        if (auto it = symbol_to_material.find(symbol); it != symbol_to_material.end()) material_id = it->second;
        else material_id = symbol;
        std::string texture;
        if (auto it = ctx.material_texture.find(material_id); it != ctx.material_texture.end()) texture = it->second;
        std::string material_name = material_id;
        if (auto it = ctx.material_name.find(material_id); it != ctx.material_name.end()) material_name = it->second;
        bool two_sided = false;
        if (auto it = ctx.material_two_sided.find(material_id); it != ctx.material_two_sided.end()) two_sided = it->second;
        ModelPart& part = ctx.sink.part(texture, material_name, two_sided);

        // Index streams: one <p> for triangles/polylist, several for polygons.
        std::vector<std::vector<int>> plists;
        for (const auto& c : prim.children)
            if (c.name == "p") plists.push_back(ints_of(c.text));
        std::vector<int> vcount;
        if (const XmlNode* vc = prim.child("vcount")) vcount = ints_of(vc->text);

        auto read = [&](const std::vector<int>& p, size_t corner, const Input* in, int comps, float* out) {
            const size_t at = corner * size_t(stride) + size_t(in->offset);
            if (at >= p.size()) return false;
            const size_t base = size_t(p[at]) * size_t(in->source->stride);
            for (int k = 0; k < comps; ++k) out[k] = base + size_t(k) < in->source->data.size() ? in->source->data[base + size_t(k)] : 0.0f;
            return true;
        };
        auto emit = [&](const std::vector<int>& p, const std::vector<size_t>& corners) {
            // Fan-triangulate the polygon given by corner indices into p.
            for (size_t i = 1; i + 1 < corners.size(); ++i) {
                const size_t tri[3] = {corners[0], corners[i], corners[i + 1]};
                vec3 pos[3], nrm[3];
                vec2 uv[3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) {
                    float v[3] = {0, 0, 0};
                    if (!read(p, tri[k], pos_in, 3, v)) { ok = false; break; }
                    pos[k] = vec3(world * vec4(v[0], v[1], v[2], 1.0f));
                    if (nrm_in && read(p, tri[k], nrm_in, 3, v)) nrm[k] = normal_m * vec3(v[0], v[1], v[2]);
                    float t[2] = {0, 0};
                    if (uv_in) read(p, tri[k], uv_in, 2, t);
                    uv[k] = vec2(t[0], t[1]);
                }
                if (!ok) continue;
                if (mirrored) {
                    std::swap(pos[1], pos[2]);
                    std::swap(nrm[1], nrm[2]);
                    std::swap(uv[1], uv[2]);
                }
                PartSink::triangle(part, pos, nrm, uv, nrm_in != nullptr);
            }
        };
        if (prim.name == "triangles") {
            if (plists.empty()) continue;
            const auto& p = plists[0];
            const size_t n = p.size() / size_t(stride);
            for (size_t t = 0; t + 2 < n; t += 3) emit(p, {t, t + 1, t + 2});
        } else if (prim.name == "polylist") {
            if (plists.empty()) continue;
            const auto& p = plists[0];
            size_t corner = 0;
            for (int count : vcount) {
                std::vector<size_t> corners;
                for (int k = 0; k < count; ++k) corners.push_back(corner + size_t(k));
                emit(p, corners);
                corner += size_t(std::max(count, 0));
            }
        } else { // polygons: one <p> per polygon
            for (const auto& p : plists) {
                std::vector<size_t> corners;
                for (size_t k = 0; k < p.size() / size_t(stride); ++k) corners.push_back(k);
                emit(p, corners);
            }
        }
    }
}

void visit_node(DaeContext& ctx, const XmlNode& node, const mat4& parent, int depth) {
    if (depth > 64) return;
    const mat4 world = parent * node_transform(node);
    auto bind_materials = [&](const XmlNode& inst) {
        std::unordered_map<std::string, std::string> map;
        std::vector<const XmlNode*> ims;
        inst.find_all("instance_material", ims);
        for (const XmlNode* im : ims) map[im->attr_or("symbol")] = strip_hash(im->attr_or("target"));
        return map;
    };
    for (const auto& c : node.children) {
        if (c.name == "instance_geometry") {
            if (const XmlNode* g = ctx.get(c.attr_or("url"))) add_geometry(ctx, *g, world, bind_materials(c));
        } else if (c.name == "instance_controller") {
            // Skinned mesh: use the bind pose of the skin's source geometry.
            if (const XmlNode* ctrl = ctx.get(c.attr_or("url"))) {
                mat4 bind(1.0f);
                const XmlNode* skin = ctrl->child("skin");
                const XmlNode* morph = ctrl->child("morph");
                std::string source;
                if (skin) {
                    source = skin->attr_or("source");
                    if (const XmlNode* bsm = skin->child("bind_shape_matrix")) {
                        const auto v = floats_of(bsm->text);
                        if (v.size() == 16)
                            for (int r = 0; r < 4; ++r)
                                for (int col = 0; col < 4; ++col) bind[col][r] = v[size_t(r * 4 + col)];
                    }
                } else if (morph) {
                    source = morph->attr_or("source");
                }
                const XmlNode* g = ctx.get(source);
                // A controller may chain to another controller.
                if (g && g->name == "controller" && g->child("skin")) g = ctx.get(g->child("skin")->attr_or("source"));
                if (g && g->name == "geometry") add_geometry(ctx, *g, world * bind, bind_materials(c));
            }
        } else if (c.name == "instance_node") {
            if (const XmlNode* n = ctx.get(c.attr_or("url"))) visit_node(ctx, *n, world, depth + 1);
        } else if (c.name == "node") {
            visit_node(ctx, c, world, depth + 1);
        }
    }
}

std::optional<ModelData> load_dae(const std::string& path, const std::string& text, std::string* error) {
    XmlNode root;
    if (!parse_xml(text, root) || root.name != "COLLADA") {
        if (error) *error = "not a COLLADA file: " + path;
        return std::nullopt;
    }
    DaeContext ctx;
    ctx.root = &root;
    ctx.dir = fs::path(path).parent_path();
    ctx.sink.out = &ctx.model;
    ctx.index(root);

    if (const XmlNode* asset = root.child("asset")) {
        if (const XmlNode* up = asset->child("up_axis")) {
            const std::string u = trim(up->text);
            if (u == "Z_UP") ctx.axis = mat4(vec4(1, 0, 0, 0), vec4(0, 0, -1, 0), vec4(0, 1, 0, 0), vec4(0, 0, 0, 1)); // (x,y,z) -> (x, z, -y)
            else if (u == "X_UP") ctx.axis = mat4(vec4(0, -1, 0, 0), vec4(1, 0, 0, 0), vec4(0, 0, 1, 0), vec4(0, 0, 0, 1));
        }
        if (const XmlNode* unit = asset->child("unit")) ctx.unit = std::max(1e-6f, float(std::atof(unit->attr_or("meter", "1").c_str())));
    }
    // Images and materials.
    if (const XmlNode* lib = root.child("library_images")) {
        std::vector<const XmlNode*> images;
        lib->find_all("image", images);
        for (const XmlNode* im : images) {
            std::string file;
            if (const XmlNode* init = im->find("init_from")) {
                file = trim(init->text);
                if (file.empty())
                    if (const XmlNode* ref = init->child("ref")) file = trim(ref->text);
            }
            const std::string resolved = resolve_texture(file, ctx.dir);
            if (const char* id = im->attr("id")) ctx.image_path[id] = resolved;
        }
    }
    std::unordered_map<std::string, std::string> effect_tex;
    if (const XmlNode* lib = root.child("library_effects"))
        for (const auto& eff : lib->children)
            if (eff.name == "effect")
                if (const char* id = eff.attr("id")) effect_tex[id] = effect_texture(ctx, eff);
    if (const XmlNode* lib = root.child("library_materials"))
        for (const auto& mat : lib->children) {
            if (mat.name != "material") continue;
            const char* id = mat.attr("id");
            if (!id) continue;
            ctx.material_name[id] = mat.attr_or("name", id);
            if (const XmlNode* ie = mat.child("instance_effect")) {
                auto it = effect_tex.find(strip_hash(ie->attr_or("url")));
                if (it != effect_tex.end()) ctx.material_texture[id] = it->second;
                // Common exporter extras for two-sided materials.
                if (const XmlNode* eff = ctx.get(ie->attr_or("url")))
                    if (const XmlNode* ds = eff->find("double_sided")) ctx.material_two_sided[id] = trim(ds->text) == "1";
            }
        }
    // Scene.
    const XmlNode* scene = nullptr;
    if (const XmlNode* sc = root.child("scene"))
        if (const XmlNode* ivs = sc->child("instance_visual_scene")) scene = ctx.get(ivs->attr_or("url"));
    if (!scene)
        if (const XmlNode* lib = root.child("library_visual_scenes")) scene = lib->child("visual_scene");
    const mat4 base = glm::scale(ctx.axis, vec3(ctx.unit));
    if (scene) {
        for (const auto& n : scene->children)
            if (n.name == "node") visit_node(ctx, n, base, 0);
    }
    if (ctx.model.parts.empty()) {
        // No scene: take every geometry as is.
        if (const XmlNode* lib = root.child("library_geometries"))
            for (const auto& g : lib->children)
                if (g.name == "geometry") add_geometry(ctx, g, base, {});
    }
    if (ctx.model.parts.empty()) {
        if (error) *error = "no geometry in " + path;
        return std::nullopt;
    }
    finish(ctx.model);
    return std::move(ctx.model);
}

} // namespace

namespace {

// DDS: uncompressed BGRA/RGBA or BC1/BC2/BC3 (DXT1/3/5), top mip only.
std::optional<ImageData> load_dds(const std::string& path) {
    const auto data = read_file(path);
    if (!data || data->size() < 128 || data->compare(0, 4, "DDS ") != 0) return std::nullopt;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data->data());
    auto u32 = [&](size_t at) { return uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 | uint32_t(bytes[at + 2]) << 16 | uint32_t(bytes[at + 3]) << 24; };
    const int height = int(u32(12)), width = int(u32(16));
    const uint32_t pf_flags = u32(80), fourcc = u32(84), bit_count = u32(88);
    const uint32_t rmask = u32(92), gmask = u32(96), bmask = u32(100), amask = u32(104);
    size_t offset = 128;
    std::string cc(reinterpret_cast<const char*>(bytes + 84), 4);
    if (cc == "DX10") {
        if (data->size() < 148) return std::nullopt;
        const uint32_t dxgi = u32(128);
        // DXGI_FORMAT_BC1_* 70..72, BC2 73..75, BC3 76..78, R8G8B8A8 27..29, B8G8R8A8 86..88
        if (dxgi >= 70 && dxgi <= 72) cc = "DXT1";
        else if (dxgi >= 73 && dxgi <= 75) cc = "DXT3";
        else if (dxgi >= 76 && dxgi <= 78) cc = "DXT5";
        else if (dxgi >= 27 && dxgi <= 29) cc = "RGBA";
        else if (dxgi >= 86 && dxgi <= 88) cc = "BGRA";
        else return std::nullopt;
        offset = 148;
    } else if (!(pf_flags & 0x4)) { // DDPF_FOURCC not set: uncompressed
        cc = bit_count == 32 ? (rmask == 0xff ? "RGBA" : "BGRA") : bit_count == 24 ? "BGR" : "";
        if (cc.empty()) return std::nullopt;
    }
    if (width <= 0 || height <= 0) return std::nullopt;
    ImageData img;
    img.width = width;
    img.height = height;
    img.rgba.assign(size_t(width) * size_t(height) * 4, 255);
    if (cc == "RGBA" || cc == "BGRA" || cc == "BGR") {
        const size_t bpp = cc == "BGR" ? 3 : 4;
        if (data->size() < offset + bpp * size_t(width) * size_t(height)) return std::nullopt;
        for (size_t i = 0; i < size_t(width) * size_t(height); ++i) {
            const uint8_t* s = bytes + offset + i * bpp;
            uint8_t* d = &img.rgba[i * 4];
            if (cc == "RGBA") { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
            else { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = bpp == 4 ? s[3] : 255; }
        }
        (void)gmask; (void)bmask; (void)amask;
        return img;
    }
    const bool dxt1 = cc == "DXT1", dxt3 = cc == "DXT3", dxt5 = cc == "DXT5";
    if (!dxt1 && !dxt3 && !dxt5) return std::nullopt;
    const size_t block_bytes = dxt1 ? 8 : 16;
    const int bw = (width + 3) / 4, bh = (height + 3) / 4;
    if (data->size() < offset + block_bytes * size_t(bw) * size_t(bh)) return std::nullopt;
    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x >= width || y >= height) return;
        uint8_t* d = &img.rgba[(size_t(y) * size_t(width) + size_t(x)) * 4];
        d[0] = r; d[1] = g; d[2] = b; d[3] = a;
    };
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* blk = bytes + offset + (size_t(by) * size_t(bw) + size_t(bx)) * block_bytes;
            uint8_t alpha[16];
            for (auto& a : alpha) a = 255;
            if (dxt3) {
                for (int i = 0; i < 16; ++i) alpha[i] = uint8_t(((blk[i / 2] >> ((i % 2) * 4)) & 0xf) * 17);
                blk += 8;
            } else if (dxt5) {
                const int a0 = blk[0], a1 = blk[1];
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i) bits |= uint64_t(blk[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i) {
                    const int code = int((bits >> (3 * i)) & 7);
                    int a;
                    if (code == 0) a = a0;
                    else if (code == 1) a = a1;
                    else if (a0 > a1) a = ((8 - code) * a0 + (code - 1) * a1) / 7;
                    else if (code == 6) a = 0;
                    else if (code == 7) a = 255;
                    else a = ((6 - code) * a0 + (code - 1) * a1) / 5;
                    alpha[i] = uint8_t(a);
                }
                blk += 8;
            }
            const uint32_t c0 = uint32_t(blk[0]) | uint32_t(blk[1]) << 8, c1 = uint32_t(blk[2]) | uint32_t(blk[3]) << 8;
            auto expand = [](uint32_t c, uint8_t* out) {
                out[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
                out[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
                out[2] = uint8_t((c & 31) * 255 / 31);
            };
            uint8_t col[4][3];
            expand(c0, col[0]);
            expand(c1, col[1]);
            bool punch = false;
            if (c0 > c1 || !dxt1) {
                for (int k = 0; k < 3; ++k) {
                    col[2][k] = uint8_t((2 * col[0][k] + col[1][k]) / 3);
                    col[3][k] = uint8_t((col[0][k] + 2 * col[1][k]) / 3);
                }
            } else {
                for (int k = 0; k < 3; ++k) {
                    col[2][k] = uint8_t((col[0][k] + col[1][k]) / 2);
                    col[3][k] = 0;
                }
                punch = true;
            }
            const uint32_t idx = uint32_t(blk[4]) | uint32_t(blk[5]) << 8 | uint32_t(blk[6]) << 16 | uint32_t(blk[7]) << 24;
            for (int i = 0; i < 16; ++i) {
                const int ci = int((idx >> (2 * i)) & 3);
                const uint8_t a = punch && ci == 3 ? 0 : alpha[i];
                put(bx * 4 + i % 4, by * 4 + i / 4, col[ci][0], col[ci][1], col[ci][2], a);
            }
        }
    return img;
}

} // namespace

std::optional<ImageData> load_image(const std::string& path) {
    if (lower(fs::path(path).extension().string()) == ".dds") return load_dds(path);
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) return std::nullopt;
    ImageData img;
    img.width = w;
    img.height = h;
    img.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    return img;
}

std::optional<ModelData> load_model(const std::string& path, std::string* error) {
    const auto text = read_file(path);
    if (!text) {
        if (error) *error = "cannot read " + path;
        return std::nullopt;
    }
    const std::string ext = lower(fs::path(path).extension().string());
    if (ext == ".obj") return load_obj(path, *text, error);
    if (ext == ".dae") return load_dae(path, *text, error);
    if (error) *error = "unsupported model format: " + path;
    return std::nullopt;
}

void transform_model(ModelData& model, const mat4& m) {
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
    const bool mirrored = glm::determinant(glm::mat3(m)) < 0.0f;
    for (auto& part : model.parts) {
        for (auto& v : part.mesh.vertices) {
            v.pos = vec3(m * vec4(v.pos, 1.0f));
            v.normal = glm::normalize(nm * v.normal);
        }
        if (mirrored)
            for (size_t i = 0; i + 2 < part.mesh.indices.size(); i += 3) std::swap(part.mesh.indices[i + 1], part.mesh.indices[i + 2]);
    }
    finish(model);
}

} // namespace gk::render
