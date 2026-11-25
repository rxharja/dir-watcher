Name:           dir-watcher
Version:        0.1.0
Release:        1%{?dist}
Summary:        Unix rule-based directory watching utility

License:        MIT
URL:            https://github.com/rxharja/dir-watcher
Source0:        https://github.com/rxharja/dir-watcher/releases/download/0.1.0/dir-watcher-0.1.0.tar.gz

BuildRequires:  make
BuildRequires:  gcc

%description
A Unix utility for watching a directory and applying rules that match a filename, extension, or dirname.


%prep
%autosetup


%build
%make_build


%install
%make_install PREFIX=%{_prefix}


%files
%license LICENSE.md
%{_bindir}/dir-watcher
%{_datadir}/dir-watcher/

%changelog
* Wed Nov 25 2026 Redon Xharja <redon.xharja@solvusglobal.com> -0.1.0-1
- Initial package
