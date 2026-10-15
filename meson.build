project('mauikit-documents', 'cpp', 'c', version: '4.0.5', meson_version: '>=1.0.0', default_options: ['cpp_std=c++17', 'c_std=gnu99', 'buildtype=release'])

cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.get_id() == 'gcc' and cpp_compiler.has_argument('-Wno-sfinae-incomplete')
    add_project_arguments('-Wno-sfinae-incomplete', language: 'cpp')
endif
qt6 = import('qt6')
qt = dependency('qt6', version: '>=6.4', modules: ['Core', 'Gui', 'Quick', 'Qml', 'Multimedia', 'Sql', 'Network', 'Concurrent'])

kf6_include_prefix = get_option('prefix') / 'include' / 'KF6'
kf6_core = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KCoreAddons'),
    dependencies: [cpp_compiler.find_library('KF6CoreAddons')],
)
kf6_i18n = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KI18n'),
    dependencies: [cpp_compiler.find_library('KF6I18n')],
)
kf6_config = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KConfig', kf6_include_prefix / 'KConfigCore'),
    dependencies: [cpp_compiler.find_library('KF6ConfigCore')],
)
mauikit = dependency('MauiKit4', version: '>=4.0.5')
c_args = host_machine.cpu_family() == 'x86_64' ? ['-march=x86-64-v3'] : []
c_args += ['-fomit-frame-pointer', '-D_FILE_OFFSET_BITS=64']
cpp_args = host_machine.cpu_family() == 'x86_64' ? ['-march=x86-64-v3'] : []
cpp_args += ['-Wno-deprecated-declarations']
poppler_dep = dependency('poppler-qt6', required: true)
kf6_archive = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KArchive'),
    dependencies: [cpp_compiler.find_library('KF6Archive')],
)
kf6_kio = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KIO', kf6_include_prefix / 'KIOCore', kf6_include_prefix / 'KIOGui', kf6_include_prefix / 'KIOWidgets'),
    dependencies: [cpp_compiler.find_library('KF6KIOCore'), cpp_compiler.find_library('KF6KIOGui')],
)
kf6_guiaddons = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KGuiAddons'),
    dependencies: [cpp_compiler.find_library('KF6GuiAddons')],
)
kf6_iconthemes = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KIconThemes'),
    dependencies: [cpp_compiler.find_library('KF6IconThemes')],
)
kf6_filemetadata = declare_dependency(
    include_directories: include_directories(kf6_include_prefix / 'KFileMetaData'),
    dependencies: [cpp_compiler.find_library('KF6FileMetaData')],
)
zlib = dependency('zlib', required: true)
documents_export_header = configure_file(
    input: 'meson-content.in',
    output: 'documents_export.h',
    configuration: {'CONTENT': ('#pragma once\n#define DOCUMENTS_EXPORT __attribute__((visibility("default")))\n')},
)
documents_version_header = configure_file(
    input: 'meson-content.in',
    output: 'documents_version.h',
    configuration: {'CONTENT': ('#pragma once\n#define Documents_VERSION_STRING "' + meson.project_version() + '"\n#define GIT_BRANCH "Stable"\n#define GIT_COMMIT_HASH "' + meson.project_version() + '"\n')},
)
poppler_cpp = run_command('find', 'src/code/poppler/code', '-type', 'f', '-name', '*.cpp', check: true).stdout().strip().split('\n')
poppler_moc_headers = run_command('sh', '-c', 'find src/code/poppler -type f -name "*.h" -print | xargs grep -l Q_OBJECT', check: true).stdout().strip().split('\n')
poppler_qt = qt6.preprocess(qresources: ['src/code/poppler/data/fonts/fonts.qrc'], moc_headers: poppler_moc_headers, dependencies: [qt])
poppler = static_library('MauiKitDocumentsPoppler', poppler_cpp + poppler_qt, dependencies: [qt, poppler_dep], include_directories: include_directories('src/code/poppler'), cpp_args: cpp_args, pic: true)
acbf_cpp = run_command('find', 'src/code/cbz/code/acbf', '-type', 'f', '-name', '*.cpp', check: true).stdout().strip().split('\n')
acbf_debug_header = configure_file(
    input: 'meson-content.in',
    output: 'acbf_debug.h',
    configuration: {'CONTENT': ('#pragma once\n#include <QLoggingCategory>\nQ_DECLARE_LOGGING_CATEGORY(ACBF_LOG)\n')},
)
acbf_debug_source = configure_file(
    input: 'meson-content.in',
    output: 'acbf_debug.cpp',
    configuration: {'CONTENT': ('#include "acbf_debug.h"\nQ_LOGGING_CATEGORY(ACBF_LOG, "org.kde.peruse.acbf")\n')},
)
acbf_moc_headers = run_command('sh', '-c', 'find src/code/cbz/code/acbf -type f -name "*.h" -print | xargs grep -l Q_OBJECT', check: true).stdout().strip().split('\n')
acbf_moc = qt6.preprocess(moc_headers: acbf_moc_headers, dependencies: [qt])
unarr_c = run_command('find', 'src/code/cbz/code/karchive-rar/unarr', '-type', 'f', '-name', '*.c', check: true).stdout().strip().split('\n')
rar_cpp = run_command('find', 'src/code/cbz/code/karchive-rar', '-type', 'f', '-name', '*.cpp', check: true).stdout().strip().split('\n')
acbf = static_library('MauiKitDocumentsAcbf', [acbf_debug_header, acbf_debug_source] + acbf_cpp + acbf_moc, dependencies: [qt], include_directories: include_directories('src/code/cbz/code/acbf'), cpp_args: cpp_args, pic: true)
rar = static_library('MauiKitDocumentsRar', rar_cpp + unarr_c, dependencies: [qt, kf6_archive, zlib], include_directories: include_directories('src/code/cbz/code/karchive-rar', 'src/code/cbz/code/karchive-rar/unarr'), cpp_args: cpp_args, c_args: c_args, pic: true)
cbz_cpp = run_command('find', 'src/code/cbz/code', '-maxdepth', '1', '-type', 'f', '-name', '*.cpp', check: true).stdout().strip().split('\n')
cbz_moc_headers = run_command('sh', '-c', 'find src/code/cbz/code -maxdepth 1 -type f -name "*.h" -print | xargs grep -l Q_OBJECT', check: true).stdout().strip().split('\n')
cbz_moc = qt6.preprocess(moc_headers: cbz_moc_headers, dependencies: [qt])
cbz = static_library('MauiKitDocumentsCbz', cbz_cpp + cbz_moc, dependencies: [qt, kf6_core, kf6_i18n, kf6_config, kf6_archive, kf6_kio, kf6_guiaddons, kf6_iconthemes, kf6_filemetadata, mauikit, zlib], link_with: [acbf, rar], include_directories: include_directories('src/code/cbz/code', 'src/code/cbz/code/acbf', 'src/code/cbz/code/karchive-rar'), cpp_args: cpp_args + ['-DKFILEMETADATA_FOUND'], pic: true)
lib = shared_library('MauiKitDocuments4', [documents_export_header, documents_version_header] + files('src/code/moduleinfo.cpp'), dependencies: [qt, kf6_core, kf6_i18n, kf6_config, kf6_archive, kf6_kio, kf6_guiaddons, kf6_iconthemes, kf6_filemetadata, mauikit, poppler_dep, zlib], include_directories: include_directories('src/code'), cpp_args: cpp_args, version: '4.0.5', soversion: '4', install: true)
plugin_moc = qt6.preprocess(moc_headers: ['src/code/plugin.h'], dependencies: [qt])
plugin_qt = qt6.preprocess(qresources: ['src/code/poppler/img_assets.qrc'])
qml_dir = get_option('libdir') / 'qt6/qml/org/mauikit/documents'
plugin = shared_module('MauiKitDocuments4plugin', ['src/code/plugin.cpp'] + plugin_moc + plugin_qt, link_with: [lib, poppler, cbz], dependencies: [qt, kf6_core, kf6_i18n, kf6_config, kf6_archive, kf6_kio, kf6_guiaddons, kf6_iconthemes, kf6_filemetadata, mauikit, poppler_dep, zlib], include_directories: include_directories('src/code', 'src/code/poppler', 'src/code/cbz/code'), cpp_args: cpp_args, install: true, install_dir: qml_dir)
install_data(configure_file(
    input: 'meson-content.in',
    output: 'qmldir',
    configuration: {'CONTENT': ('''module org.mauikit.documents
plugin MauiKitDocuments4plugin
''')}
), install_dir: qml_dir)
install_subdir('src/controls.6/poppler', install_dir: qml_dir / 'poppler', strip_directory: true)
install_subdir('src/controls.6/cbz', install_dir: qml_dir / 'cbz', strip_directory: true)
install_headers(files('src/code/moduleinfo.h') + [documents_export_header, documents_version_header], subdir: 'MauiKit4/Documents')
cmake_targets = configure_file(
    input: 'meson-content.in',
    output: 'MauiKitDocuments4Targets.cmake',
    configuration: {'CONTENT': (('''get_filename_component(_MAUIKIT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET MauiKitDocuments4)
  add_library(MauiKitDocuments4 SHARED IMPORTED)
  set_target_properties(MauiKitDocuments4 PROPERTIES
    IMPORTED_LOCATION "${_MAUIKIT_PREFIX}/@LIBDIR@/libMauiKitDocuments4.so.4.0.5"
    INTERFACE_INCLUDE_DIRECTORIES "${_MAUIKIT_PREFIX}/include/MauiKit4/Documents")
endif()
if(NOT TARGET MauiKit4::Documents)
  add_library(MauiKit4::Documents ALIAS MauiKitDocuments4)
endif()
if(NOT TARGET MauiKit::Documents)
  add_library(MauiKit::Documents ALIAS MauiKitDocuments4)
endif()
''')).replace('@LIBDIR@', (get_option('libdir')))}
)
cmake_config = configure_file(
    input: 'meson-content.in',
    output: 'MauiKitDocuments4Config.cmake',
    configuration: {'CONTENT': ('''include(CMakeFindDependencyMacro)
find_dependency(Qt6 6.4 COMPONENTS Core)
include("${CMAKE_CURRENT_LIST_DIR}/MauiKitDocuments4Targets.cmake")
set(MauiKitDocuments4_FOUND TRUE)
''')}
)
cmake_version = configure_file(
    input: 'meson-content.in',
    output: 'MauiKitDocuments4ConfigVersion.cmake',
    configuration: {'CONTENT': (('''set(PACKAGE_VERSION "@VERSION@")
if(PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_EXACT TRUE)
endif()
''')).replace('@VERSION@', (meson.project_version()))}
)
install_data([cmake_targets, cmake_config, cmake_version], install_dir: get_option('libdir') / 'cmake' / 'MauiKitDocuments4')
