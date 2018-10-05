# DbPicSync
Lets you convert any files/folders to pictures to store on free unlimited amazon photo storage.

[![Build Status](https://travis-ci.org/0xFEEDC0DE64/DbPicSync.svg?branch=master)](https://travis-ci.org/0xFEEDC0DE64/DbPicSync) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/430be3218ad04825b1fb594121d473c6)](https://www.codacy.com/app/0xFEEDC0DE64/DbPicSync?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=0xFEEDC0DE64/DbPicSync&amp;utm_campaign=Badge_Grade)

Amazon offers prime customers 5GB free cloud storage in [amazon drive](https://www.amazon.com/clouddrive) and unlimited storage for photos.

This tool lets you convert any file/folder structure from your existing NAS or external HDD into such photos!

One of those generated photos can look like this:
![Example generated from raspberry os](https://raw.githubusercontent.com/0xFEEDC0DE64/DbPicSync/master/example.png)

The generated folders looks like this:
![The folder for raspberry os](https://raw.githubusercontent.com/0xFEEDC0DE64/DbPicSync/master/folder.png)

After uploading tons of random data your storage usage might look like this:
![Hundrets of gigabytes use no storage on amazon](https://raw.githubusercontent.com/0xFEEDC0DE64/DbPicSync/master/amazon.png)

## Building from source
This project can only be built as part of the project structure [DbSoftware](https://github.com/0xFEEDC0DE64/DbSoftware)

```Shell
git clone https://github.com/0xFEEDC0DE64/DbSoftware.git
cd DbSoftware
git submodule update --init --recursive DbPicSync
cd ..
mkdir build_DbSoftware
cd build_DbSoftware
qmake CONFIG+=ccache ../DbSoftware
make -j$(nproc) sub-DbPicSync
make sub-DbPicSync-install_subtargets
./bin/picsync
```
