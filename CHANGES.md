# Loader Alice — changements par rapport à soloader-boilerplate

Fork de [v-atamanenko/soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate),
à partir de la révision `0a382f0ebce348c8cdc68f32fb5fa3186baddc38` (2026-07-22), licence MIT ([LICENSE](LICENSE)).
L'historique git détaille chaque changement ; ce fichier en donne les raisons, regroupées par sujet.

Référence de tout ce qui suit : l'inventaire du jalon 1, `spike-c/SYMBOLS.md` du dépôt du port
(Anezium/Alice-American-McGees-PSVita-Port ; « Pièges » = section « Ce que l'inventaire dit au loader » de
`spike-c/JALON1.md`), relu contre les sources réellement compilées dans le `.so` : miniquad 0.4.8 **après** le
patch du packager du fork, `src/android.rs` du fork (`6a9b8cd`), cpal 0.15.3, oboe 0.6.1 (crate + C++),
rodio 0.20.1.

## Branches

| Branche | Contenu |
|---|---|
| `upstream-fixes` | `0a382f0` + 13 corrections génériques, une par commit, chacune compile seule : `struct stat` bionic, `__errno`, `lseek64`, `pthread_once`, piles forcées à 512 Kio, logs Release, `sysconf`, `getrandom`, `posix_memalign`, `syscall` (futex + getrandom), `dlsym`/`dlopen`, `__assert2` et tables de déroulement, `__sF`. Proposable upstream telle quelle. |
| `alice` (par défaut) | `upstream-fixes` + tout ce qui est propre à Alice / Looking Glass : `config.h`, couche EGL, Android 26 et OpenSL muet, imports nouveaux, `java.c`, environnement et chemins, séquence d'activité miniquad, autres corrections du boilerplate, outils, tests et docs. |

## Organisation

| Élément | Ici |
|---|---|
| `so_util`, `vitaGL`, `FalsoNDK` | sous-modules, comme upstream, aux mêmes révisions : so_util `c4732373`, vitaGL `9c23758f`, FalsoNDK `7dceb3bb` (inutilisé, `NDK_PORT` OFF) |
| `FalsoJNI` | sous-module vers [Anezium/FalsoJNI](https://github.com/Anezium/FalsoJNI), branche `fix-call-method-a` = `083d5a07` (v1.4, la révision d'upstream) + le correctif des `Call*MethodA` (voir la fin de ce fichier) |
| `fios`, `sha1`, `libc_bridge` | code dans `lib/`, comme upstream |
| `kubridge.h` + stubs | ceux du VitaSDK (paquet vdpm `kubridge`, présent dans l'image softfp) ; `lib/kubridge` d'upstream reste, inutilisé |
| PNG LiveArea | `extras/livearea/*.png`, ceux d'upstream |
| Paramètres | `source/config.h`, surcharge par `-DLOADER_EXTRA_DEFS="-D…"` ; `DATA_PATH`/`SO_PATH`/`VITAGL_MAKE_FLAGS` en cache CMake |

Cloner avec `git clone --recursive`, ou `git submodule update --init --recursive` après coup.

Application : `ALICESOL1`, « Alice Looking Glass », `alice_soloader.vpk`. `.so` attendu :
`ux0:data/alice/liblooking_glass.so` ; données (`pak*.pk3` du joueur) : `ux0:data/alice/`.
vitaGL (LGPL-3.0) reste lié statiquement comme dans tous les ports so-loader.

## Corrections demandées (a → k)

### a. `syscall` : futex et getrandom — `reimpl/sys.c`, `reimpl/futex.c`

Pourquoi (piège 1) : sur `target_os = "android"`, `Mutex`, `RwLock`, `Once`, `Condvar` et `thread::park` de Rust std
appellent `syscall(SYS_futex, …)`. Le boilerplate exposait `void syscall(int)` : r0 indéfini, aucun blocage réel.

- `syscall_soloader(n, a1..a6)` : en AAPCS les arguments entiers variadiques passent comme des arguments fixes
  (r0-r3 puis pile), une définition à 7 paramètres les lit donc correctement.
- 240 `SYS_futex` et 422 `SYS_futex_time64` : `FUTEX_WAIT`, `FUTEX_WAIT_BITSET`, `FUTEX_WAKE`, `FUTEX_WAKE_BITSET`,
  avec ou sans `FUTEX_PRIVATE_FLAG`, échéance absolue `CLOCK_MONOTONIC` (ou `FUTEX_CLOCK_REALTIME`) pour
  `WAIT_BITSET`, relative pour `WAIT`. Retours : 0 / `EAGAIN` (valeur différente) / `ETIMEDOUT` ; `WAKE` renvoie le
  nombre de réveillés. Les autres opérations (`REQUEUE`, `WAKE_OP`…) : log + `ENOSYS` (std ne les utilise pas).
- 384 `SYS_getrandom` → `getrandom_soloader` ; 224 `gettid` ; 391 `copy_file_range` et 340 `splice` → `ENOSYS`
  (loggé une fois) ; tout autre numéro → log avec l'adresse de l'appelant + `-1/ENOSYS`.

Primitive choisie : **16 seaux haché par adresse, chacun = `SceKernelLwMutex` + `SceKernelLwCond` + liste de
waiters sur la pile**. WAIT vérifie `*uaddr == val` et s'inscrit sous le verrou du seau, WAKE marque jusqu'à n
waiters de la même adresse sous ce même verrou puis `SignalLwCondAll` : pas de réveil perdu, aucune allocation,
32 objets noyau créés une fois (`futex_init()` avant le chargement du `.so`).
Compromis : un WAKE réveille aussi les waiters d'autres adresses du même seau (réveils parasites, ils revérifient
leur marque et se rendorment) — négligeable avec la dizaine de threads du jeu. Écartés : event flags (un objet
par futex, impossible à indexer par adresse sans table dynamique), un sémaphore par attente (création/destruction
noyau à chaque attente, ou cache par thread), `pthread_cond` de pthread-embedded (couche de plus, horloge
`CLOCK_REALTIME` pour les échéances). Les échéances sont calculées sur `sceKernelGetProcessTimeWide()`, la même
horloge que `clock_gettime(CLOCK_MONOTONIC)` du loader, d'où Rust tire ses échéances absolues.
Test hôte : `tests/host/run.sh` (shim pthread des appels Sce) — EAGAIN, échéance de 50 ms, 8 threads × 200 000
verrous selon le protocole exact du `Mutex` futex de std : OK.

### b. `dlsym`, `dlopen`, `dlclose`, `dlerror` — `dynlib.c`

Pourquoi (piège 2) : `dlsym_soloader` renvoyait `&default_dynlib[i].func` (adresse de l'entrée de table) ;
miniquad charge **tout EGL** par `dlopen("libEGL.so")` + `dlsym`. Et `dlopen` → `ret1`, `dlclose`/`dlerror` → `ret0`.

- `soloader_lookup_symbol(name)` : 1) table du loader (les hooks de shaders et la couche EGL gagnent), 2) pour
  `gl*`, `vglGetProcAddress` (qui retire les suffixes EXT/OES/ARB), 3) NULL. Sert `dlsym` **et** `eglGetProcAddress`.
- `dlsym` renvoie la fonction ; en dernier recours il cherche aussi les exports du `.so` (`so_symbol`), pour
  `RTLD_DEFAULT`. Un symbole absent est loggé et positionne `dlerror()`.
- `dlopen` : `libEGL.so`, `libEGL.so.1`, `libGLESv1_CM.so`, `libGLESv2.so`, `libGLESv3.so`, `libandroid.so`,
  `libOpenSLES.so`, `liblog.so`, `libc.so`, `libm.so`, `libdl.so` (et le `.so` lui-même) → poignée factice non nulle ;
  `dlopen(NULL)` → poignée « programme ». Tout autre nom → NULL + `dlerror()`, en particulier **`libaaudio.so`**
  (oboe reste sur OpenSL ES). `dlclose` → 0.
- Au chargement, `report_unresolved()` liste les imports indéfinis que ni la table ni vitaGL ne servent.
  Attendu au premier boot : `copy_file_range` et `splice` seulement (« import check: 2 unresolved »).

### c. `sysconf` — `reimpl/sys.c`

Pourquoi (piège 3) : `sysconf` → `ret0` ; `std::thread` l'appelle pour la taille de page. Constantes bionic
(`libc/include/bits/sysconf.h`) : `_SC_PAGESIZE`/`_SC_PAGE_SIZE` → 4096 ; `_SC_NPROCESSORS_ONLN/CONF` → **3**
(`LOADER_NPROCESSORS`) : la Vita a 4 Cortex-A9 mais le cœur 3 est réservé au système, 3 est ce qu'une appli peut
occuper (sert à `available_parallelism()`, donc à la taille des pools de threads) ; `_SC_PHYS_PAGES` → 512 Mio,
`_SC_AVPHYS_PAGES` → 256 Mio ; `_SC_CLK_TCK` 100, `_SC_OPEN_MAX`/`_SC_IOV_MAX` 1024, `_SC_THREAD_STACK_MIN` 16 Kio,
`_SC_GETPW/GR_R_SIZE_MAX` 1024, `_SC_ARG_MAX` 128 Kio ; le reste → log + `-1/EINVAL`.

### d. `getrandom` — `reimpl/sys.c`

Pourquoi (piège 4) : import faible (API 28) absent ; std le teste pour la graine de `HashMap` et, à défaut,
tente `syscall(384)` puis `/dev/urandom` → panic au premier `HashMap`. `getrandom_soloader` remplit par blocs de
64 octets avec `sceKernelGetRandomNumber`, ignore `GRND_NONBLOCK`/`GRND_RANDOM` (le RNG ne bloque jamais).
Servi par la table **et** par `syscall(384)`.

### e. `posix_memalign`, `aligned_alloc`, `memalign` — `reimpl/mem.c`

Pourquoi (piège 6) : l'allocateur global Rust (`__rdl_alloc`, `__rdl_realloc`, `__rdl_alloc_zeroed`) appelle
`posix_memalign` pour tout alignement > 8. newlib a `memalign()` (libéré par `free()`), donc pas de
sur-allocation : validation (puissance de deux, multiple de `sizeof(void*)`), codes d'erreur **bionic** renvoyés
(`EINVAL` 22, `ENOMEM` 12 ; `posix_memalign` ne touche pas errno). `aligned_alloc` idem avec errno ; `memalign`
corrige un alignement invalide comme bionic.

### f. `copy_file_range` et `splice` non fournis — `dynlib.c`

Pourquoi (piège 5) : imports faibles dont std teste l'adresse (`R_ARM_GLOB_DAT`). Lecture de `so_util.c` :
`so_relocate` ne touche pas un `GLOB_DAT` non défini ; `so_resolve` (compilé avec `HAVE_VITAGL`) essaie la table puis
`vglGetProcAddress`, qui renvoie NULL pour un nom non `gl*`, et **écrit cette valeur nulle** dans le GOT ; un
`JUMP_SLOT` non résolu reçoit `plt0_stub` (log + abort si appelé). `so_resolve_with_dummy` aurait au contraire mis
`_so_util_ret0` (non nul) sur les noms de la table : écarté. Mécanisme retenu : **absents de la table** → pointeur
NULL → std `syscall(391/340)` → `ENOSYS` (point a) → repli `read`/`write`. `sendfile` (import fort) → `-1/ENOSYS`
loggé une fois, même repli.

### g. Les 23 imports nouveaux — `dynlib.c`, `reimpl/sys.c`, `reimpl/io.c`, `reimpl/android.c`

| Import | Traitement |
|---|---|
| `__register_atfork` | accepté (0), loggé une fois : pas de `fork()` sur Vita, les handlers ne peuvent pas tourner |
| `android_set_abort_message` | log `fatal` du message (c'est le message d'`abort_message` de libc++abi) |
| `clock_nanosleep` | réel : relatif ou `TIMER_ABSTIME` (via notre `clock_gettime`), `sceKernelDelayThread`, retourne un code d'erreur |
| `closelog`, `openlog` | log (`syslog` part dans le log du loader) |
| `syslog` | réel : formatage + log `error` |
| `dirfd` | newlib `dirfd` (le `DIR` Vita a un fd), log si < 0 |
| `fchmod` | succès + log une fois (pas de permissions POSIX sur Vita) |
| `gettid` | `sceKernelGetThreadId()` |
| `posix_memalign` | point e |
| `readlink` | `/proc/self/exe` → `SO_PATH` (`std::env::current_exe`), sinon log + `-1/EINVAL` |
| `readv`, `writev` | réels (boucle `read`/`write`, arrêt sur lecture courte) ; `writev` sur fd 1/2 va au log |
| `sendfile` | `-1/ENOSYS`, loggé une fois (point f) |
| `splice` | **absent exprès** (point f) |
| `SL_IID_ANDROIDCONFIGURATION`, `SL_IID_RECORD` | données réelles de la libOpenSLES du VitaSDK (comme les 4 autres `SL_IID_*`) |
| `ANativeWindow_fromSurface`, `ANativeWindow_release` | fenêtre factice unique avec compteur de références (`reimpl/android.c`) ; `ANativeWindow_setBuffersGeometry` (jusqu'ici seulement sous `NDK_PORT`) logue et renvoie 0 |
| `getrandom` | point d |
| `copy_file_range` | **absent exprès** (point f) |
| `dl_iterate_phdr` | réel : 1 module, en-têtes de programme recopiés depuis l'image chargée (so_util libère le fichier, `mod->phdr` pend) |
| `tanhf` | newlib |

### h. EGL / GLES — `reimpl/egl.c`, `dynlib.c`

Le `egl.c` du boilerplate n'était même pas compilé (l'EGL de vitaGL était utilisé). Nouvelle couche, fonctions
préfixées `alice_egl*` pour ne jamais entrer en conflit avec l'`egl.o` de `libvitaGL.a` :

- les **28** fonctions EGL que miniquad résout existent toutes (son `declare_module!` fait `unwrap()` sur chaque
  `dlsym` : un seul NULL = panic avant la première image), dont les 11 rares en stubs loggants qui échouent
  proprement (`eglCreatePbufferSurface`/`PixmapSurface` → `EGL_NO_SURFACE` + `EGL_BAD_MATCH`, `eglBindTexImage`,
  `eglReleaseTexImage`, `eglCopyBuffers` → `EGL_BAD_SURFACE`, `eglSurfaceAttrib`/`eglWaitNative` loggés, `eglWaitGL`
  → `glFinish`, `eglSwapInterval` → `vglWaitVblankStart`) ;
- `eglChooseConfig` : 0 config si `EGL_RENDERABLE_TYPE`/`EGL_CONFORMANT` contient `EGL_OPENGL_ES3_BIT_KHR` ; sinon
  1 config RGBA8888/profondeur 24/stencil 8 ; `EGL_SAMPLES`/`EGL_SAMPLE_BUFFERS` acceptés et ignorés (loggés) ;
- `eglCreateContext` : `EGL_CONTEXT_CLIENT_VERSION ≥ 3` → `EGL_NO_CONTEXT` + `EGL_BAD_MATCH` ; le patch du packager
  réessaie alors en GLES2 ;
- `eglInitialize` démarre vitaGL (`gl_init()`, idempotent) : c'est le premier appel EGL du thread de rendu de
  miniquad, le contexte GXM vit donc sur le thread qui émet tous les appels GL ;
- `eglGetProcAddress` → `soloader_lookup_symbol` (point b) : `glShaderSource`/`glCompileShader` passent par les
  hooks du boilerplate, le reste par la table puis vitaGL.

Bilan statique (`tools/check_symbols.py`, sur SYMBOLS.md) : EGL 28/28, **GLES2 99/99** (96 vitaGL ou table,
`glStencilMaskSeparate` ajouté, et `glBlendColor`/`glDetachShader`/`glTexParameterfv` qui étaient des `ret0`
silencieux et deviennent des stubs loggants — `glTexParameterfv` relaie `glTexParameterf` pour les paramètres
scalaires).

**Écart assumé avec la consigne « GLES3 non résolus »** : miniquad appelle `glGenVertexArrays` et
`glBindVertexArray` sans condition dans `GlContext::new()`, et **chaque** `draw()` passe par
`glDrawElementsInstanced` (même avec 1 instance) ; un NULL = panic `Option::unwrap()` dès la création du contexte.
vitaGL implémente ces fonctions : les **13** noms GLES3 que vitaGL fournit sont servis (`glBindVertexArray`,
`glGenVertexArrays`, `glDeleteVertexArrays`, `glDrawElementsInstanced`, `glDrawArraysInstanced`,
`glVertexAttribDivisor`, `glBlitFramebuffer`, `glReadBuffer`, `glGetStringi`, `glGenQueries`, `glDeleteQueries`,
`glBeginQuery`, `glEndQuery`). Les **10** autres étaient laissés NULL : `glClearBuffer{fi,fv,uiv}`, `glCompressedTexImage3D`,
`glTexImage3D`, `glTexSubImage3D`, `glFramebufferTextureLayer`, `glRenderbufferStorageMultisample` (cibles MSAA),
`glDrawBuffers` (MRT > 1 seulement), `glVertexAttribIPointer` (jamais pris : macroquad garde
`gl_pass_as_float = true`). `glGetString(GL_VERSION)` de vitaGL vaut `"OpenGL ES 2.0 VitaGL"`, donc miniquad se
met en mode GL2 (pas d'instancing, GLSL 100). Les 64 `AAudio*` restent NULL (et `dlopen("libaaudio.so")` échoue).

Après START GAME, miniquad 0.4.8 de la voie Android fait `Option::unwrap()` dans `src/native/gl.rs`
sur la première fonction dont `eglGetProcAddress` a rendu NULL.
Le backend natif (`Anezium/miniquad`, branche `vita-0.4.8`, `src/native/vita/gl_loader.rs`) remplace quatre noms
absents de vitaGL et arrive au menu. Les mêmes remplaçants sont dans `reimpl/egl.c`, enregistrés dans
`default_dynlib[]`, donc servis par `dlsym` et par `eglGetProcAddress` :

| Nom | Comportement | Log |
|---|---|---|
| `glDrawBuffers` | no-op (un seul attachement couleur) | une fois, au premier appel |
| `glBlendColor` | no-op (déjà en place avant ce correctif) | une fois, au premier appel |
| `glRenderbufferStorageMultisample` | `glRenderbufferStorage`, samples ignoré | une fois, au premier appel |
| `glVertexAttribIPointer` | `glVertexAttribPointer` avec `normalized = GL_FALSE` | une fois, au premier appel |

Restent NULL, non inventés : `glTexImage3D`, `glClearBuffer{fi,fv,uiv}`, `glFramebufferTextureLayer`,
`glCompressedTexImage3D`, `glTexSubImage3D`, `glGetQueryObjectui64v`. `tools/check_symbols.py` classe les
quatre ci-dessus `shim` et ces noms `absent`.

Correction de l'inventaire (SYMBOLS.md / JALON1.md piège 9) : le patch du packager remplace
`EGL_SAMPLES, sample_count` par `0x3040, 4`, c'est-à-dire **`EGL_RENDERABLE_TYPE = EGL_OPENGL_ES2_BIT`**, pas
`EGL_SAMPLES = 4`. miniquad ne demande donc pas de MSAA ; l'ignorer reste la règle si un autre chemin le demande.
MSAA vitaGL : `LOADER_VGL_MSAA` = 0 (le boilerplate forçait 4x).

### i. Android 26 et OpenSL ES muet — `reimpl/sys.c`, `java.c`, `reimpl/android.c`

- `__system_property_get` (le boilerplate répondait « psvita » à tout) : `ro.build.version.sdk` = **26**
  (`LOADER_ANDROID_SDK`), `ro.build.version.release` 8.0.0, `ro.build.version.codename` REL, fabricant/modèle ; nom
  inconnu → chaîne vide comme bionic, loggé. oboe : `isAAudioRecommended()` exige ≥ 27, il reste sur OpenSL ES.
- FalsoJNI : champ statique `SDK_INT` = 26 (lu par oboe-rs via `android/os/Build$VERSION`).
- Les 6 imports OpenSLES sont des **données** (`SL_IID_*`), pas des fonctions : ils pointent vers les IID réels de
  la libOpenSLES du VitaSDK. La fonction est `slCreateEngine`, qu'oboe obtient par
  `dlsym(dlopen("libOpenSLES.so"))` : stub loggant qui renvoie `SL_RESULT_FEATURE_UNSUPPORTED` (`LOADER_AUDIO_ENABLED=0`),
  ou la vraie fonction du VitaSDK avec `LOADER_AUDIO_ENABLED=1` (non testé).
- En amont d'OpenSL : cpal appelle `AudioTrack.getMinBufferSize(III)I` par JNI et **`unwrap()`** le résultat
  (méthode manquante = panic). Elle existe donc dans `java.c` et renvoie `ERROR_BAD_VALUE` (-2) : cpal n'a aucune
  configuration, `OutputStream::try_default()` échoue, oboe-rs tente l'énumération (`getSystemService` absent →
  erreur JNI propre), rodio rend `Err`, le jeu affiche « No audio output available » et continue
  (`Audio::new`, `src/audio.rs` du fork). Premier boot muet sans toucher au code Rust.

### j. Piles et mémoire configurables — `reimpl/pthr.c`, `config.h`, `utils/glutil.c`, `main.c`

Pourquoi (piège 7) : miniquad demande 16 Mio pour son thread de rendu et le fork fait
`set_var("RUST_MIN_STACK", "16777216")`. Le boilerplate, lui, **ignorait toute demande** : `pthread_create_soloader`
forçait 512 Kio sur chaque thread — trop peu pour le thread de rendu de Looking Glass.

- `pthread_attr_setstacksize` mémorise la demande ; `pthread_create` l'applique, plafonnée à
  `LOADER_THREAD_STACK_CAP` = 8 Mio (logué quand on plafonne), `LOADER_THREAD_STACK_DEFAULT` = 1 Mio sans demande.
  Chaque création est loguée (point d'entrée, taille).
- `getenv("RUST_MIN_STACK")` vaut `LOADER_RUST_MIN_STACK` = 2 Mio (défaut Android de Rust) ; le `setenv` du fork
  est ignoré et logué. Sans cela chaque `std::thread` aurait 16 Mio puis 8 après plafond.
- Thread « UI » (celui qui joue l'activité, voir k) : `LOADER_MAIN_STACK_SIZE` = 8 Mio, priorité
  `LOADER_UI_THREAD_PRIORITY` = 96.
- Mémoire : `LOADER_NEWLIB_HEAP_SIZE` (256 Mio en mémoire étendue, 192 sinon), mode mémoire étendue
  `LOADER_EXTENDED_MEM` (option CMake, `ATTRIBUTE2=12`, ON), vitaGL `vglInitExtended(LOADER_VGL_LEGACY_POOL,
  960, 544, LOADER_VGL_RAM_THRESHOLD = 16 Mio, LOADER_VGL_MSAA)` — seuil relevé de 6 à 16 Mio car les piles des
  threads Rust créés après vitaGL sont prises sur cette réserve.

### k. JNI et séquence d'activité — `main.c`, `java.c`

Lu dans miniquad (`native/android.rs` patché) et le `MainActivity.java` du fork (`tools/android/`) :

1. `JNI_OnLoad(&jvm, NULL)` : miniquad mémorise la `JavaVM`.
2. `Java_quad_1native_QuadNative_activityOnCreate(env, cls, activity)` : `NewGlobalRef(activity)` puis
   `quad_main()` → `init_runtime()` du fork (`ndk_context` via `GetJavaVM`, hook de panic) → `main()` de Looking
   Glass → `miniquad::start` → `run()` : `setFullScreen(Z)V` si plein écran, crée le canal de messages **dans un
   thread_local du thread appelant**, lance le thread de rendu (16 Mio demandés) et **revient**.
3. `surfaceOnSurfaceChanged(env, cls, surface, 960, 544)` — le `MainActivity` du fork n'envoie jamais
   `surfaceOnSurfaceCreated`, seulement `surfaceChanged`. Le thread de rendu tourne en attente active sur
   `try_recv()` jusqu'à ce message, puis `dlopen("libEGL.so")`, `create_egl_context`, chargement GL par
   `eglGetProcAddress`, `ANativeWindow_setBuffersGeometry`, `eglCreateWindowSurface`, `eglMakeCurrent`, boucle.
4. `activityOnResume(env, cls)`.
5. Boucle d'entrée à 60 Hz sur le même thread : `controls_poll()` → `MainActivity.nativeOnGamepad(connected=1,
   buttons, lt, rt, lx, ly, rx, ry)` au format XInput du fork (Croix = A 0x1000, Rond = B, Carré = X, Triangle = Y,
   L/R = LB/RB, Start, Select = Back, L2/R2 PS TV = gâchettes 255, L3/R3, axes ±32767 avec Y vers le haut), et
   l'écran tactile avant → `surfaceOnTouch(id, phase, x, y)` (phases miniquad : 2 début, 0 déplacement, 1 fin).

Tout `QuadNative.*` doit venir **du même thread** que `activityOnCreate` (thread_local du canal) : d'où un thread
UI unique créé par `main()` (pile choisie) plutôt que le thread principal Sce. `nativeOnGamepad` n'écrit que des
atomiques et pourrait venir de n'importe où.

Méthodes Java servies par FalsoJNI (recherche par nom, toutes classes) : `showKeyboard(Z)V` (log, pas encore
d'IME), `setFullScreen(Z)V`, `reportNativeCrash(String)V` (log fatal ; le hook du fork écrit aussi `crash.log`),
`finish()V` (`sceKernelExitProcess`), `getClipboardText` (null), `setClipboardText`, `getAssets` (objet factice,
inatteignable : pas d'AAsset dans les imports), les 7 actions du lanceur Android (`openFolderPicker`,
`openFilePicker`, `openTelegramLink`, `openGithubLink`, `showCreditsDialog`, `enterGameRenderMode`,
`checkForUpdates`, loggées), `AudioTrack.getMinBufferSize` (point i) ; champs `SDK_INT`, `WINDOW_SERVICE`.
Absente exprès : `getSystemService` (oboe-rs traite l'erreur JNI).

Chemins : Looking Glass cherche sa racine inscriptible dans `LOOKING_GLASS_SETTINGS_DIR` puis
`LOOKING_GLASS_ANDROID_STORAGE`, et ses PK3 dans `LOOKING_GLASS_DATA` (puis `base/`, `Alice1/bin/base`…). Le loader
a maintenant un vrai environnement (`getenv`/`setenv`/`unsetenv`, table interne) pré-rempli : ces trois variables
= `ux0:data/alice`, `TMPDIR` = `ux0:data/alice/tmp`, `HOME`. Filet de sécurité : `open`, `fopen`, `stat`, `opendir`,
`mkdir` réécrivent `/storage/emulated/0/Android/data/com.lookingglass.alice/files` (et les variantes `/sdcard`,
`/data/user/0`, `/data/data`) vers `ux0:data/alice` (logué).

## Autres défauts du boilerplate corrigés en route

| Défaut | Effet sur ce `.so` | Correction |
|---|---|---|
| `struct stat` bionic déclarée `packed` (disposition i386) | `Metadata::len()` lisait `st_size` à l'offset 44 au lieu de 48 (ARM EABI aligne les `long long`), `nlink_t`/`uid_t` newlib 16 bits | `io.h` : disposition bionic ARM exacte (104 octets), `_Static_assert` sur chaque offset ; idem `struct dirent` (280 octets, `d_ino` 64 bits rempli) |
| `__errno()` renvoyait un pointeur **dans la table de traduction** | std fait `*__errno() = 0` avant `readdir` : la table était corrompue pour le reste de la partie | emplacement errno bionic par thread (`__thread`), errno newlib traduit puis consommé |
| `lseek64` → `ret0` | tout `File::seek` de Rust renvoyait 0 sans bouger (lecteur zip/PK3 cassé) | `lseek` newlib (32 bits) + `EOVERFLOW` au-delà |
| `pthread_once` : test-and-set puis retour immédiat | un 2e thread pouvait utiliser l'état pas encore initialisé (libc++abi, emutls) | états 0/1/2, attente active douce |
| `__assert2` → `ret0` | une assertion C/C++ ratée continuait | log fatal + `abort()` |
| `dl_unwind_find_exidx` → `ret0` | libunwind sans tables (exceptions C++ d'oboe, backtraces) | renvoie `.ARM.exidx` du module si `pc` y est |
| `__sF` = tableau de `FILE` newlib | `&__sF[2]` (stderr bionic, `FILE` de 84 octets) ne tombait pas sur le 3e élément ; avec `USE_SCELIBC_IO` un `FILE*` newlib partait vers SceLibc | `sF_bionic[3][84]` reconnu par `fprintf`/`vfprintf`/`fputc`/`fwrite`/`fflush`, sortie vers le log |
| stdout/stderr | `eprintln!` Rust → `write(2)` newlib, invisible | `write`/`writev` sur fd 1/2 → `sceClibPrintf` (PrincessLog) |
| `sceClibPrintf(buffer)` dans le logger | un `%` dans un message du jeu = format interprété | `sceClibPrintf("%s", …)`, création du mutex du logger au démarrage (`_log_init`) |
| `l_warn`/`l_info` compilés à vide en Release | stubs silencieux de fait, logs `ALOG` du jeu perdus | `l_notice`, `l_stub`, `l_stub_once` toujours actifs ; `__android_log_*` toujours imprimés (`LOADER_ALOG_ALWAYS`) |
| `prctl` → `ret0`, `getenv`/`setenv` stubs | — | `PR_SET_NAME`/`PR_GET_NAME` gérés, reste loggé + `EINVAL` ; environnement réel |
| `mmap` | un mapping de fichier rendait de la mémoire nulle | anonyme seulement (aligné 4 Kio, zéro), fichier → `MAP_FAILED/ENODEV` loggé |
| Patch kuser aveugle sur tout `.text` | 9 Mio de code Rust : risque de réécrire un littéral 0xFFFF0FC0 | `LOADER_KUSER_PATCH=0` : on compte et on logue seulement (Rust/LLVM armv7 n'utilise pas les helpers kuser) |
| `utils.c` `file_load` | argument `printf` manquant | corrigé |
| Contrôles | L2/R2/L3/R3 (PS TV) absents | ajoutés |

## Piège trouvé dans FalsoJNI v1.4 (corrigé dans le fork, contournement retiré)

`Call*MethodA` convertissait le tableau `jvalue` avec `_AtoV(0, args)`, qui renvoie un `va_list` pointant dans
**son propre cadre de pile, déjà détruit** au retour, et qui ne contiendrait de toute façon que le pointeur
`jvalue*`, pas les valeurs. Or la crate Rust `jni` (cpal, oboe-rs) appelle **toujours** les variantes `*A`.

Correctif dans [Anezium/FalsoJNI](https://github.com/Anezium/FalsoJNI), branche `fix-call-method-a`
(`890eb0b`) : `GetMethodID`/`GetStaticMethodID` mémorisent la signature de chaque méthode, et chaque variante
`*A` reconstruit dans son propre cadre la zone qu'un appel variadique aurait poussée (promotions par défaut,
emplacements de 4 octets, valeurs 64 bits alignées sur 8, convention AAPCS lue par `va_arg`), puis la passe à
la méthode sous forme de `va_list`. Les implémentations ne changent pas. Vérifié sur armv7 Linux (gcc 12 sous
qemu) avec un banc jetable : 55 contrôles passent, 42 échouaient avant.

Contournement retiré : `AudioTrack.getMinBufferSize` lit maintenant ses trois arguments (fréquence, masque de
canaux, format). Ils apparaissent dans le log, et avec `LOADER_AUDIO_ENABLED=1` la taille renvoyée en dépend
(environ 20 ms, au moins 4096 octets) au lieu d'être fixée à 4096.

## Reste à faire côté loader (non bloquant pour le premier log)

- Suspension / reprise (`activityOnPause`/`Resume` sur les événements `sceAppMgr`), touche Back Android,
  clavier IME (`showKeyboard`), pavé tactile arrière.
- Audio réel : `LOADER_AUDIO_ENABLED=1` (les arguments JNI de cpal/oboe passent depuis le correctif FalsoJNI
  ci-dessus), ou backend audio côté Rust.
- Horloge réelle : la constante `__epoch` du boilerplate (`62135587294000000`) décale `CLOCK_REALTIME` de 9 506 s
  (~2 h 38) si le tick RTC compte bien depuis l'an 1 en UTC (vraie valeur `62135596800000000`, à vérifier au banc) ; sans effet sur le jeu (horodatage seulement), laissé tel quel.

## 2026-10-11 — gl_init: vglInitExtended return value

`vglInitExtended` returns GL_FALSE on a normal init and GL_TRUE only when the resolution was clamped (vitaGL `source/vgl.c`, `res_fallback`). `gl_init` treated GL_FALSE as a failure and aborted with "vitaGL initialisation failed (not enough memory?)" on the first run on hardware. Now logged, never fatal.
