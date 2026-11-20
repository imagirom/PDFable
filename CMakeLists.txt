cmake_minimum_required(VERSION 3.21)
project(PDFable VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(PDFABLE_SIGNING "Certificate-based (PAdES) signing and validation; needs OpenSSL" OFF)
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Network)
if(PDFABLE_SIGNING)
  find_package(Qt6 REQUIRED COMPONENTS Network)
  find_package(OpenSSL REQUIRED)
endif()

# MuPDF: vcpkg port "libmupdf" on Windows, or -DMUPDF_ROOT=<prefix> elsewhere.
find_package(unofficial-libmupdf CONFIG QUIET)
add_library(mupdf_dep INTERFACE)
if(TARGET unofficial::libmupdf::libmupdf)
  target_link_libraries(mupdf_dep INTERFACE unofficial::libmupdf::libmupdf)
else()
  find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h HINTS ${MUPDF_ROOT}/include REQUIRED)
  find_library(MUPDF_LIBRARY NAMES mupdf libmupdf HINTS ${MUPDF_ROOT}/lib REQUIRED)
  find_library(MUPDF_THIRD_LIBRARY NAMES mupdf-third HINTS ${MUPDF_ROOT}/lib)
  target_include_directories(mupdf_dep INTERFACE ${MUPDF_INCLUDE_DIR})
  target_link_libraries(mupdf_dep INTERFACE ${MUPDF_LIBRARY})
  if(MUPDF_THIRD_LIBRARY)
    target_link_libraries(mupdf_dep INTERFACE ${MUPDF_THIRD_LIBRARY})
  endif()
endif()

add_library(pdfable_core STATIC
  src/core/Document.cpp src/core/Document.h
  src/core/Stroke.cpp src/core/Stroke.h)
target_include_directories(pdfable_core PUBLIC src/core)
target_link_libraries(pdfable_core PUBLIC Qt6::Gui mupdf_dep)
if(PDFABLE_SIGNING)
  target_sources(pdfable_core PRIVATE src/core/Signer.cpp src/core/Signer.h)
  target_compile_definitions(pdfable_core PUBLIC PDFABLE_SIGNING)
  target_link_libraries(pdfable_core PUBLIC Qt6::Network OpenSSL::Crypto)
endif()
if(MSVC)
  target_compile_options(pdfable_core PUBLIC /utf-8 /W3)
  target_compile_definitions(pdfable_core PUBLIC _USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(pdfable_core PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-clobbered)
endif()

add_executable(PDFable WIN32
  src/main.cpp
  src/ui/MainWindow.cpp src/ui/MainWindow.h
  src/ui/DocumentTab.cpp src/ui/DocumentTab.h
  src/ui/PdfView.cpp src/ui/PdfView.h
  src/ui/RenderWorker.cpp src/ui/RenderWorker.h
  src/ui/Dialogs.cpp src/ui/Dialogs.h
  src/ui/Theme.cpp src/ui/Theme.h
  src/ui/Creator.cpp src/ui/Creator.h
  resources/resources.qrc)
if(WIN32)
  target_sources(PDFable PRIVATE resources/app.rc)
endif()
target_include_directories(PDFable PRIVATE src/ui)
target_link_libraries(PDFable PRIVATE pdfable_core Qt6::Widgets Qt6::PrintSupport Qt6::Network)

option(PDFABLE_TESTS "Build the headless self-test" OFF)
if(PDFABLE_TESTS)
  add_executable(pdfable_tests tests/tests.cpp
    src/ui/PdfView.cpp src/ui/PdfView.h src/ui/RenderWorker.cpp src/ui/RenderWorker.h
    src/ui/DocumentTab.cpp src/ui/DocumentTab.h src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/Dialogs.cpp src/ui/Dialogs.h src/ui/Theme.cpp src/ui/Theme.h src/ui/Creator.cpp src/ui/Creator.h resources/resources.qrc)
  target_include_directories(pdfable_tests PRIVATE src/ui)
  target_link_libraries(pdfable_tests PRIVATE pdfable_core Qt6::Widgets Qt6::PrintSupport)
endif()
