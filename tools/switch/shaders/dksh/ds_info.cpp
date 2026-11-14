// ds_info: each library shader's runtime info, computed on the PC, as the console's cache/info_shaders.bin.
//
// The deko3d renderer needs three things per shader before its first draw (RenderDk::InfoDe): the SPIR-V's fingerprint
// (the DKSH cache key), the specialization bits it reads, and for a pixel shader the interpolator locations it reads
// (which picks the vertex shader variant). The last one costs a whole SPIR-V to GLSL conversion, 5-10 ms a shader on
// the console's ring thread: Singularity's lightning, drawing ~150 shaders for the first time in one frame, froze for
// a second. The console saves what it computes in cache/info_shaders.bin; this writes that file for every shader of the
// library at once, with the same functions (rex::deko3d), so not even a first session computes it.
//
// Usage: ds_info <singularity_shaders.dslib> <info_shaders.bin> [--hilos N]
// Format (little-endian, the console's RenderDk::GuardarInfoSiHaceFalta): u32 "INFO", u32 count, then per shader
// u64 container fingerprint, u64 SPIR-V fingerprint, u32 specialization mask, u32 interpolators, u8 interpolators valid.

#include "ds_shader_library.h"

#include <rex/ui/deko3d/cache_dksh.h>
#include <rex/ui/deko3d/glsl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kMagiaInfo = 0x4F464E49;  // "INFO"

struct Info {
  uint64_t contenedor = 0;
  uint64_t huella = 0;
  uint32_t mascara = 0;
  uint32_t interpoladores = 0;
  uint8_t listos = 0;
};

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 3) {
    std::fprintf(stderr, "uso: ds_info <singularity_shaders.dslib> <info_shaders.bin> [--hilos N]\n");
    return 1;
  }
  unsigned hilos = std::max(1u, std::thread::hardware_concurrency());
  for (int i = 3; i + 1 < argc; i += 2) {
    if (std::string(argv[i]) == "--hilos") hilos = std::max(1, std::atoi(argv[i + 1]));
    else throw std::runtime_error(std::string("opcion desconocida: ") + argv[i]);
  }
  const auto inicio = std::chrono::steady_clock::now();
  ds::BibliotecaShaders biblioteca;
  biblioteca.Cargar(std::filesystem::path(argv[1]));
  const auto& shaders = biblioteca.shaders();
  if (shaders.empty()) throw std::runtime_error("la biblioteca no tiene shaders");

  std::vector<Info> infos(shaders.size());
  std::atomic<size_t> siguiente{0}, fallidos{0};
  auto trabajar = [&] {
    for (size_t i; (i = siguiente.fetch_add(1)) < shaders.size();) {
      const ds::Shader& s = shaders[i];
      Info& info = infos[i];
      info.contenedor = s.huella;
      info.huella = rex::deko3d::HuellaSpirv(s.spirv);
      info.mascara = rex::deko3d::MascaraEspecializacion(s.spirv);
      if (!s.vertices) {
        const rex::deko3d::GlslDeko g = rex::deko3d::SpirvAGlsl(s.spirv, 0);
        if (g.glsl.empty()) {
          ++fallidos;  // left without interpolators: the console converts this one itself
          continue;
        }
        info.interpoladores = g.interpoladores;
        info.listos = 1;
      }
    }
  };
  std::vector<std::thread> grupo;
  for (unsigned h = 0; h < hilos; ++h) grupo.emplace_back(trabajar);
  for (auto& t : grupo) t.join();

  std::FILE* f = std::fopen(argv[2], "wb");
  if (!f) throw std::runtime_error(std::string("no se puede escribir ") + argv[2]);
  const uint32_t cabecera[2] = {kMagiaInfo, uint32_t(infos.size())};
  bool ok = std::fwrite(cabecera, sizeof(cabecera), 1, f) == 1;
  for (const Info& i : infos) {
    ok = ok && std::fwrite(&i.contenedor, 8, 1, f) == 1 && std::fwrite(&i.huella, 8, 1, f) == 1 &&
         std::fwrite(&i.mascara, 4, 1, f) == 1 && std::fwrite(&i.interpoladores, 4, 1, f) == 1 &&
         std::fwrite(&i.listos, 1, 1, f) == 1;
  }
  ok = std::fclose(f) == 0 && ok;
  if (!ok) throw std::runtime_error(std::string("escritura incompleta de ") + argv[2]);
  const size_t ps = std::count_if(shaders.begin(), shaders.end(), [](const ds::Shader& s) { return !s.vertices; });
  std::printf("%zu shaders (%zu pixel shaders, %zu sin interpoladores) -> %s en %.1f s\n", infos.size(), ps,
              fallidos.load(), argv[2],
              std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count());
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "ds_info: %s\n", e.what());
  return 1;
}
