#
# spec file for cats-core
#
Name:          lthor
Summary:       Flashing tool for Tizen lunchbox
Version:       1.0
Release:       1
Group:         Development/Tools/Other
License:       Samsung reserved
URL:           https://download.tizendev.org/tools/lthor/
Source0:       %{name}_%{version}.tar.gz

BuildRequires:  libarchive-devel
BuildRequires:  cmake
BuildRequires:  pkg-config

%description
Tool for downloading binaries from a Linux host PC to a target phone.
It uses a USB cable as a physical communication medium.
It is prerequisite that the boot-loader should support download protocol
which is compatible with 'lthor'.

%prep 
%setup -q

%build
cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr .
make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf %{buildroot}

%post

%files
%defattr(-,root,root)
%{_bindir}/%{name}

%changelog

