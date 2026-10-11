# Spike C — préparation du jalon 2 : loader Vita (compilation pure)

> Déplacé le 2026-10-11 depuis `spike-c/JALON2-PREP.md` du dépôt du port (Anezium/Alice-American-McGees-PSVita-Port),
> où ce dépôt est le sous-module `spike-c/loader`. Depuis, le code vendoré est devenu ce fork : les dépendances
> sont des sous-modules (`tools/fetch_deps.sh` supprimé) et FalsoJNI pointe sur un fork corrigé. Les chemins
> `spike-c/…` ci-dessous sont ceux du dépôt du port. Le reste est le compte rendu d'origine.

Statut : **le loader compile et produit un VPK** avec la VitaSDK softfp ; toutes les corrections identifiées au
jalon 1 (`spike-c/SYMBOLS.md`, `spike-c/JALON1.md`) y sont intégrées. **Rien n'a été lancé sur console** :
ce n'est pas un résultat au sens du banc (règle 3 du README). Agent cloud, sans Vita, 2026-10-11.

Code : ce dépôt, branche `alice` (fork de soloader-boilerplate `0a382f0`, MIT). Détail et justification de
chaque modification : [CHANGES.md](../CHANGES.md).

## Résultat

| | Valeur |
|---|---|
| VPK | `alice_soloader.vpk`, **732 494 octets**, TITLE_ID `ALICESOL1`, « Alice Looking Glass » |
| SHA-256 (build natif ci-dessous) | VPK `8ea0125a20064e0d00c384076014f4b7bc1e36d2465a00e395908f40fe7cb1b1` (non reproductible à l'octet : la bannière de log contient `__DATE__`/`__TIME__`) |
| Contenu | `eboot.bin` (~647 Kio), `sce_sys/param.sfo` (ATTRIBUTE2=12), icône/LiveArea d'upstream, `cpuinfo`, `meminfo`, `urandom` |
| ELF | text 909 592 o, data 12 212 o, bss 4,1 Mio ; ABI flottante de base (softfp), `Tag_FP_arch: VFPv3` |
| `.so` attendu | `ux0:data/alice/liblooking_glass.so` (le `.so` du jalon 1, profil lto=false/panic=abort) |
| Données | `ux0:data/alice/` : les `pak*.pk3` du joueur (Looking Glass les cherche via `LOOKING_GLASS_DATA`, puis `base/`…) ; réglages, sauvegardes et `crash.log` du jeu dans le même dossier |
| Durée | build complet 26 s (4 vCPU, vitaGL compris) ; image Docker : pull de quelques minutes (~780 Mo de SDK) |

Vérifications faites sans console :

- build Release natif et build **dans l'image Docker** `docker/Dockerfile.softfp` (copie propre du dossier, vitaGL
  recompilé) : OK, mêmes tailles ;
- build Debug avec `-Wall -Wextra` : aucun avertissement dans le code nouveau ;
- `_Static_assert` sur la disposition des `struct stat`/`struct dirent` bionic ARM (offsets 16/32/48/56/64/72/96,
  104 octets ; 280 octets) ;
- `tools/check_symbols.py` (table du loader + table de vitaGL contre SYMBOLS.md) : **121/123 imports
  résolus** (manquent `copy_file_range` et `splice`, exprès), **0 stub silencieux** parmi les imports, EGL 28/28,
  GLES2 99/99, GLES3 13/23 (ceux que vitaGL implémente), AAudio 0/64 (exprès) ;
- `tests/host/run.sh` : émulation futex testée sur hôte Linux (shim pthread des appels Sce) avec le
  protocole exact du `Mutex` futex de Rust std, 8 threads × 200 000 sections critiques, EAGAIN, échéance 50 ms : OK ;
- la boilerplate d'origine, sans modification, compile aussi avec cette toolchain (référence).

## Toolchain softfp

- Pas de binaire téléchargeable dans la session : les releases de `vitasdk-softfp/autobuilds` (tag le plus récent
  `master-linux-v2.23`, révision `4283fe7`) ne sont joignables que par l'API GitHub, refusée ici (403, dépôt non
  attaché), et le nom de l'archive contient l'horodatage du build (`vitasdk-<hôte>-<date>.tar.bz2`), impossible à
  deviner.
- Trouvé à la place : l'image officielle **`atamanenko/vitasdk-softfp`** (construite par la CI de
  `vitasdk-softfp/docker`, `vdpm install-all` compris), digest
  `sha256:fa66f683d397885ffec3dcccf915ca977c19e20fc895c2d7e9ce25201c39ecdd` (tag `latest`, créé le 2026-09-20).
  Contenu : `arm-vita-eabi-gcc 10.3.0`, `--with-float=softfp --with-fpu=neon`, toolchain « Built at 2026-03-29 »
  (newlib `0da3cb1`, pthread-embedded `63e1cd9`, vita-headers `238339a`, vita-toolchain `71f3789`), libvitaGL,
  libkubridge_stub, libOpenSLES, sndfile, mathneon, vitashark… Temps pour l'obtenir : ~5 min, pas de build.
- `docker/Dockerfile.softfp` : cible par défaut = cette image épinglée par digest (testée) ; cible
  `--target from-source` = reconstruction depuis `vitasdk-softfp/buildscripts` `75e0a52` + `vdpm` `e255587`
  (non testée, secours).
- Le SDK peut aussi être extrait de l'image et utilisé nativement (Ubuntu 24.04, glibc 2.39) : c'est ce qui a servi
  ci-dessous.

## Commandes exactes

```sh
# 0. Sous-modules du loader (so_util, FalsoJNI, vitaGL, FalsoNDK), révisions figées
git submodule update --init --recursive

# 1a. Avec Docker (machine du banc)
docker build -f docker/Dockerfile.softfp -t alice-vita/softfp .
docker run --rm -v "$PWD":/work -w /work alice-vita/softfp \
  sh -c 'cmake -S spike-c/loader -B spike-c/loader/build -DCMAKE_BUILD_TYPE=Release && cmake --build spike-c/loader/build -j4'
# -> spike-c/loader/build/alice_soloader.vpk

# 1b. Natif, SDK extrait de l'image (ce qui a été fait ici)
docker create --name sf atamanenko/vitasdk-softfp@sha256:fa66f683d397885ffec3dcccf915ca977c19e20fc895c2d7e9ce25201c39ecdd
docker cp sf:/usr/local/vitasdk "$W/vitasdk-softfp" && docker rm sf
export VITASDK="$W/vitasdk-softfp" PATH="$W/vitasdk-softfp/bin:$PATH"
cmake -S spike-c/loader -B "$W/build-loader" -DCMAKE_BUILD_TYPE=Release
cmake --build "$W/build-loader" -j8

# Vérifications hors console
python3 -I spike-c/loader/tools/check_symbols.py      # couverture des symboles contre SYMBOLS.md
spike-c/loader/tests/host/run.sh                       # émulation futex sur hôte (gcc)

# Réglages sans toucher au code (voir source/config.h)
cmake ... -DLOADER_EXTRA_DEFS="-DLOADER_THREAD_STACK_CAP=4194304 -DLOADER_VGL_MSAA=1" -DLOADER_EXTENDED_MEM=OFF
# Build Debug (logs l_debug de chaque open/stat, vitaGL LOG_ERRORS=1) : -DCMAKE_BUILD_TYPE=Debug
```

Particularités de l'environnement cloud (pas à reporter ailleurs) : `dockerd` déjà lancé par l'agent du spike A ;
la sortie Docker Hub passe par le proxy de la session sans injection de CA.

## Ce qui est fait (résumé ; détail dans CHANGES.md)

- a. `syscall` : futex (`WAIT`/`WAIT_BITSET`/`WAKE`/`WAKE_BITSET`, `_PRIVATE`, `futex_time64`) sur 16 seaux
  `SceKernelLwMutex` + `SceKernelLwCond`, `getrandom` (384), `gettid` ; le reste → log + `ENOSYS`.
- b. `dlsym` renvoie la fonction ; `dlopen` connaît libEGL/libGLESv2/libGLESv3/libandroid/libOpenSLES… et refuse
  `libaaudio.so` ; `dlerror`/`dlclose` réels ; rapport des imports non résolus au chargement.
- c. `sysconf` : page 4096, **3 cœurs** (le 4e est au système), mémoire physique 512 Mio, le reste −1/EINVAL loggé.
- d. `getrandom` via `sceKernelGetRandomNumber`. e. `posix_memalign`/`aligned_alloc`/`memalign` réels.
- f. `copy_file_range`/`splice` absents : GOT nul (vérifié dans `so_util.c`) → std passe par `syscall` → `ENOSYS`
  → repli read/write.
- g. Les 23 imports nouveaux : implémentés ou stubs loggants, aucun silencieux (tableau dans CHANGES.md).
- h. Couche EGL complète (28 fonctions), refus GLES3 (config et contexte), MSAA ignoré, `eglGetProcAddress` →
  table du loader puis vitaGL. **Écart** : les 13 fonctions GLES3 que vitaGL implémente sont servies, car miniquad
  appelle `glGenVertexArrays`/`glBindVertexArray` sans condition et dessine toujours par
  `glDrawElementsInstanced` ; les 10 autres et les 64 AAudio restent NULL.
- i. Android 26 (`ro.build.version.sdk`, `SDK_INT`) ; `slCreateEngine` → `SL_RESULT_FEATURE_UNSUPPORTED` et
  `AudioTrack.getMinBufferSize` → −2 : premier boot muet, sans panic (`Audio::new` du fork l'encaisse).
- j. Piles : demandes honorées, plafond 8 Mio (logué), 1 Mio par défaut, `RUST_MIN_STACK` servi à 2 Mio ; thread UI
  8 Mio ; tas newlib, mémoire étendue, seuil et MSAA de vitaGL dans `config.h`.
- k. Séquence d'activité rejouée sur un thread « UI » unique (contrainte du thread_local de miniquad) :
  `JNI_OnLoad` → `activityOnCreate` (→ `quad_main`) → `surfaceOnSurfaceChanged(960, 544)` → `activityOnResume` →
  boucle 60 Hz : manettes → `nativeOnGamepad` (XInput), tactile → `surfaceOnTouch`. Méthodes Java servies par
  FalsoJNI : `setFullScreen`, `showKeyboard`, `reportNativeCrash`, `finish`, presse-papiers, actions du lanceur,
  `getMinBufferSize`. Environnement : `LOOKING_GLASS_SETTINGS_DIR`/`LOOKING_GLASS_DATA` = `ux0:data/alice`.
- En plus, défauts du boilerplate qui auraient cassé ce `.so` : `struct stat` bionic mal alignée (`st_size`),
  `__errno` qui corrompait sa table, `lseek64` → 0, piles forcées à 512 Kio, `pthread_once` non bloquant,
  `__assert2`/`dl_unwind_find_exidx` en `ret0`, `__sF` de mauvaise taille, logs Release muets, `%` dans les logs.
- Correction de l'inventaire : le patch du packager met `EGL_RENDERABLE_TYPE = EGL_OPENGL_ES2_BIT` (`0x3040, 4`),
  pas `EGL_SAMPLES = 4` (SYMBOLS.md/JALON1.md piège 9).

## Avant le premier boot (machine du banc)

1. Régénérer le `.so` : `spike-c/tools/build_so.sh <dossier hors dépôt>` (profil lto=false/panic=abort ; le SHA-256
   du cloud, `519f97de…aa62a`, ne se reproduit pas ailleurs : le chemin de `CARGO_HOME` est écrit dans `.rodata`), puis le copier en `ux0:data/alice/liblooking_glass.so` (FTP vitacompanion).
2. Copier les `pak*.pk3` du joueur dans `ux0:data/alice/` (jamais dans le dépôt).
3. Console : kubridge (obligatoire, sinon dialogue « kubridge.skprx is not installed ») et `libshacccg.suprx`
   (obligatoire, vérifié par `gl_preload`).
4. `vita_deploy` de `alice_soloader.vpk`, `vita_launch ALICESOL1`, `vita_logs`.
5. Log attendu, dans l'ordre (tout est visible en Release) :
   `Alice Vita loader … built` → `config: …` → `env: LOOKING_GLASS_…` → `kubridge check passed.` →
   `SO relocated.` → `import check: 2 unresolved symbol(s)` (copy_file_range, splice) → `SO imports resolved.` →
   `kuser helper scan: …` → `FalsoJNI initialized.` → `UI thread started` → `export … = 0x98…` (6 lignes) →
   `JNI_OnLoad -> 0x10006` → `activityOnCreate: entering quad_main` → `setenv("RUST_MIN_STACK" …) ignored` →
   `pthread_create(… stack 16384 KiB requested, capped to 8192 KiB` → `activityOnCreate returned` →
   `surfaceOnSurfaceChanged(960x544) sent` (le `dlopen("libEGL.so" …) -> handle` du thread de rendu peut
   s'intercaler avant : c'est sa première action) → `eglGetDisplay` → `eglInitialize` /
   `vglInitExtended(…) = 1` → `eglChooseConfig: 1 config` → `eglCreateContext: GLES 3 context refused` puis
   `GLES 2 context` → `eglCreateWindowSurface` → logs `[ALOG]` du jeu. **Jalon 2** = jusqu'à `import check` ;
   **jalon 3** = premier `[ALOG]`/`[stderr]` de Looking Glass ; **jalon 4** = premier `glClear` visible (`vita_shot`).
6. En cas de crash : `vita-parse-core` avec l'ELF `alice_loader` du build (garder le dossier de build) ; les adresses
   `0x98xxxxxx` sont dans le `.so` (soustraire `0x98000000`, symboles du `.so` non strippé).

## Risques connus (à regarder en premier si ça casse)

| Risque | Symptôme probable | Piste |
|---|---|---|
| Le thread de rendu miniquad attend `SurfaceChanged` en **boucle active** ; s'il partage le cœur du thread UI à priorité ≥ | blocage juste après `activityOnCreate returned` absent ou `surfaceOnSurfaceChanged` jamais logué | `LOADER_UI_THREAD_PRIORITY` (96 ; valeur et sémantique des priorités pthread-embedded à vérifier), ou affinité de cœur |
| Pile de 8 Mio insuffisante pour le rendu (16 demandés) | crash dans le `.so` profond dans la pile | `-DLOADER_THREAD_STACK_CAP=16777216` si la mémoire suit |
| Mémoire : tas 256 Mio + vitaGL + piles | `vglInitExtended = 0` / allocations nulles | `LOADER_NEWLIB_HEAP_SIZE`, `LOADER_VGL_RAM_THRESHOLD`, profil de perf du fork |
| Méthode Java non servie | `[JNI] GetMethodID(… "x" …): not found` puis panic `assert!(!method.is_null())` (miniquad) | ajouter la méthode dans `java.c` |
| FalsoJNI `*MethodA` (arguments illisibles) | valeurs absurdes dans une méthode appelée par la crate `jni` | voir CHANGES.md, corriger FalsoJNI |
| Shaders GLSL 100 de macroquad refusés par le traducteur de vitaGL | `glCompileShader` en échec, écran noir | build Debug (vitaGL `LOG_ERRORS=1`) |
| Mapping manette (Ext2 : L/R de la Vita = L1/R1 ?) | gâchettes/épaules inversées | `reimpl/controls.c` |

## Pas fait (après le premier boot)

Pause/reprise, touche Back, IME, pavé arrière, audio réel (OpenSL du VitaSDK ou backend Rust), icône/LiveArea
propres au projet (ce sont encore les PNG d'upstream, `extras/livearea/`), mise à jour de `docker/README.md`
(qui dit encore que `Dockerfile.softfp` est à écrire — hors périmètre de cette session).
