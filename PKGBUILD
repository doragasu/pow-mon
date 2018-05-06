#Maintainer: doragasu <doragasu@hotmail.com>

pkgname=pow-mon
pkgver=1.0
pkgrel=3
pkgdesc="Controls secondary RPi power, providing pushbutton and socket control interfaces."
arch=(armv7h)
license=(GPL)
depends=(raspberrypi-firmware)
source=(pow-mon_${pkgver}.tar.xz)
        
sha256sums=(a059dcd2d315d6cc5231aaae6407437e1f2732b06fb560acd6774d66f3a5dcd4)

build() {
    cd ${srcdir}/${pkgname}
    make
}

package() {
	cd ${srcdir}/${pkgname}
	DESTDIR=${pkgdir} make install
}

