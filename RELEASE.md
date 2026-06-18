# Synthetika Studio Release Notes

## Initial Public Release

**Synthetika Studio** adalah langkah awal menuju sebuah **Open Experience Composition Platform**: lingkungan kreatif terbuka untuk menyusun suara, visual, ruang, gerakan, dan interaksi sebagai bagian dari satu sistem komposisi.

Rilis awal ini berfungsi sebagai fondasi pengembangan. Project ini dibangun di atas basis teknis **Bespoke Synth**, lalu diarahkan ulang untuk mendukung visi Synthetika Studio sebagai platform audiovisual performance dan experience composition.

---

## Fokus Rilis

Rilis ini belum dimaksudkan sebagai produk final. Fokus utama rilis awal adalah:

- Menyediakan fondasi source code awal untuk pengembangan Synthetika Studio.
- Menyimpan basis modular real-time yang dapat dikembangkan lebih jauh.
- Menyiapkan arah konsep dari audio modular menuju experience composition.
- Membuka ruang pengembangan untuk integrasi audio, visual, spatial, motion, dan utility nodes.
- Mendokumentasikan visi awal project secara terbuka.

---

## Fondasi Teknis

Project ini mewarisi beberapa kekuatan utama dari basis Bespoke Synth:

- Node-based architecture
- Real-time audio processing
- Modular signal routing
- MIDI ecosystem
- Sequencer framework
- Open-source development model

Di atas fondasi tersebut, Synthetika Studio akan dikembangkan menuju sistem yang lebih luas, dengan kategori node seperti:

```text
Audio Nodes
Visual Nodes
Spatial Nodes
Motion Nodes
Utility Nodes
```

---

## Isi Repository

Rilis awal repository berisi:

- Source code utama aplikasi desktop berbasis C++ dan JUCE.
- Konfigurasi build CMake.
- Resource aplikasi, preset, sample, dan data contoh.
- Dependency pihak ketiga, sebagian sebagai submodule.
- Script pendukung dan installer awal.
- Dokumentasi awal project melalui `README.md`.

Folder build lokal seperti `build/`, `ignore/`, file `.obj`, `.exe`, `.dll`, `.pdb`, dan artefak kompilasi lain tidak termasuk dalam rilis repository.

---

## Status Project

Status saat ini: **early foundation / experimental development**.

Synthetika Studio masih berada pada tahap awal transformasi dari basis modular synthesizer menuju platform komposisi pengalaman. Struktur, fitur, nama modul, dan arah teknis masih dapat berubah seiring eksplorasi dan pengembangan.

---

## Roadmap Awal

Beberapa arah pengembangan berikutnya:

- Merapikan identitas project dari basis Bespoke/Syntetika lama menuju Synthetika Studio.
- Menambahkan dokumentasi build yang sesuai dengan struktur repository saat ini.
- Memetakan ulang kategori node untuk audio, visual, spatial, motion, dan utility.
- Mengeksplorasi integrasi visual real-time dan generative graphics.
- Mengeksplorasi spatial audio dan kontrol ruang.
- Menyiapkan contoh patch atau workflow untuk audiovisual performance.
- Membersihkan resource dan dependency yang tidak dibutuhkan untuk distribusi awal.

---

## Catatan Open Source

Synthetika Studio dikembangkan secara terbuka sebagai ruang eksplorasi bagi media artist, visual jockey, creative coder, musisi eksperimental, desainer pengalaman ruang, dan kreator yang tertarik pada pertemuan antara seni, teknologi, dan pengalaman manusia.

Project ini berdiri di atas kontribusi besar ekosistem open source. Rilis awal ini tetap menyertakan lisensi dan struktur dependency dari basis project yang digunakan.

---

**Synthetika Studio**  
*Compose Sound. Shape Space. Create Experience.*
