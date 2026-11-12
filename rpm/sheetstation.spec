Name:       sheetstation
Summary:    Share files directly between devices, with a Google Sheet as the only server
Version:    0.1.0
Release:    1
License:    TBD
URL:        https://github.com/IsaakRichards/SheetStation
Source0:    %{name}-%{version}.tar.bz2

Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  cmake
BuildRequires:  desktop-file-utils

%description
SheetStation shares files (e-books and the like) directly between devices over
WebRTC. A Google Sheet, through a small Apps Script web app, is used only to
find the other devices and swap connection details.

%prep
%setup -q -n %{name}-%{version}

%build
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DSHEETSTATION_SAILFISH=ON
make %{?_smp_mflags}

%install
cd build
make install DESTDIR=%{buildroot}
desktop-file-install --delete-original \
    --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
