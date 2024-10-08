#!/bin/bash

# Copyright (c) 2020 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set +e
set -x
SYSTEM=`uname -s`
if [ -z ${BRANCH} ]; then
    BRANCH="develop"
fi

if [[ "$SYSTEM" == "Linux" ]] || [[ "$SYSTEM" == "Darwin" ]];then
    PADDLE_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}")/../" && pwd )"
elif [[ "$SYSTEM" == "Windows_NT" ]];then
    PADDLE_ROOT="$(cd "$PWD/../" && pwd )"
fi
CURDIR=`pwd`
cd $PADDLE_ROOT
if [[ "$SYSTEM" == "Linux" ]] || [[ "$SYSTEM" == "Darwin" ]];then
    cp $PADDLE_ROOT/paddle/scripts/paddle_build.sh $PADDLE_ROOT/paddle/scripts/paddle_build_pre.sh
elif [[ "$SYSTEM" == "Windows_NT" ]];then
    git remote | grep upstream
    if [ $? != 0 ]; then
        git remote add upstream https://github.com/PaddlePaddle/Paddle.git
    fi
    git fetch upstream ${BRANCH}
fi
CURBRANCH=`git rev-parse --abbrev-ref HEAD`
echo $CURBRANCH
if [ `git branch | grep 'prec_added_ut'` ];then
    git branch -D 'prec_added_ut'
fi
git checkout -b prec_added_ut upstream/${BRANCH}
git branch
mkdir prec_build
cd prec_build
if [[ "$SYSTEM" == "Linux" ]] || [[ "$SYSTEM" == "Darwin" ]];then
    bash $PADDLE_ROOT/paddle/scripts/paddle_build_pre.sh cmake_gen_in_current_dir >prebuild.log 2>&1
elif [[ "$SYSTEM" == "Windows_NT" ]];then
    bash $PADDLE_ROOT/win_cmake.sh >prec_build.log 2>&1
fi
# remove line ended with .exe to get correct deleted_ut list
ctest -N | awk -F ':' '{print $2}' | sed '/^$/d' | sed '$d' | sed 's/ //g' | sed '/\.exe$/d' | grep 'test' > $PADDLE_ROOT/br-ut
#UNITTEST_DEV.spec is used for checking changes of unittests between pr and paddle_develop in the later step
spec_path_dev=${PADDLE_ROOT}/paddle/fluid/UNITTEST_DEV.spec
ctest -N | awk -F ':' '{print $2}' | sed '/^$/d' | sed '$d' > ${spec_path_dev}
cd $PADDLE_ROOT/build
spec_path_pr=${PADDLE_ROOT}/paddle/fluid/UNITTEST_PR.spec
ctest -N | awk -F ':' '{print $2}' | sed '/^$/d' | sed '$d' > ${spec_path_pr}
ctest -N | awk -F ':' '{print $2}' | sed '/^$/d' | sed '$d' | sed 's/ //g' | sed '/\.exe$/d' | grep 'test' > $PADDLE_ROOT/pr-ut
cd $PADDLE_ROOT
grep -F -x -v -f br-ut pr-ut > $PADDLE_ROOT/added_ut
if [[ "$SYSTEM" == 'Linux' ]];then
    sort pr-ut |uniq -d > $PADDLE_ROOT/duplicate_ut
fi
echo "New-UT:"
cat $PADDLE_ROOT/added_ut
rm -rf prec_build
if [[ "$SYSTEM" == "Linux" ]] || [[ "$SYSTEM" == "Darwin" ]];then
    rm $PADDLE_ROOT/br-ut $PADDLE_ROOT/pr-ut $PADDLE_ROOT/paddle/scripts/paddle_build_pre.sh
elif [[ "$SYSTEM" == "Windows_NT" ]];then
    # get the deleted ut list in windows, will be used in check_change_of_unittest.sh
    grep -F -x -v -f pr-ut br-ut > $PADDLE_ROOT/deleted_ut
    rm $PADDLE_ROOT/br-ut $PADDLE_ROOT/pr-ut $PADDLE_ROOT/win_cmake.sh
fi
git checkout -f $CURBRANCH
echo $CURBRANCH
git branch -D prec_added_ut
cd $CURDIR
