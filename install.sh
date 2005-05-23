#!/bin/sh

src='./install'
if [ -d ./dist ]; then
  src='./dist/install'
fi

if ! [ -d $src ]; then
  echo "ERROR: Could not find a valid distribution directory."
  echo "       If this is a source-only release, try 'make dist'."
  exit 1
fi

dst='/'
if [ $# -ne 0 ]; then 
  dst=$1
fi

if ! [ -d $dst ]; then
  echo "ERROR: You must specify a valid install directory."
  echo "       The specified directory '$dst' is not valid."
  exit 1
fi

echo "Installing Xen from '$src' to '$dst'..."
(cd $src; tar -cf - --exclude etc/init.d * ) | tar -C $dst -xf -
cp -fdRL $src/etc/init.d/* $dst/etc/init.d/
echo "All done."

echo "Checking to see whether prerequisite tools are installed..."
cd $src/../check
./chk install
echo "All done."

exit 0
