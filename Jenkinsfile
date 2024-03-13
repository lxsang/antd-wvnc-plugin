def build_plugin()
{
  sh '''
  set -e
  set -x
  cd $WORKSPACE
  mkdir -p build/$arch/
  [ -f Makefile ] && make clean
  case $arch in
    amd64|x86_64)
        HOST=
        ;;
    aarch64|arm64)
        HOST=--host=aarch64-linux-gnu
        ;;
    armv7l|arm)
        HOST=--host=arm-linux-gnueabihf
        ;;
    *)
        echo "Unkown architecture"
        exit 1
        ;;
  esac
  libtoolize
  aclocal
  autoconf
  automake --add-missing
  search_path=$(realpath antd/build/$arch/usr)
  CFLAGS="-I$search_path/include" \
    LDFLAGS="-L$search_path/lib" \
    ./configure $HOST --prefix=/usr
  CFLAGS="-I$search_path/include" \
  LDFLAGS="-L$search_path/lib" \
    DESTDIR=$WORKSPACE/build/$arch \
    make install
  '''
}

pipeline{
  agent {
    docker {
      image 'xsangle/ci-tools:latest'
      reuseNode true
    }
  }
  options {
    // Limit build history with buildDiscarder option:
    // daysToKeepStr: history is only kept up to this many days.
    // numToKeepStr: only this many build logs are kept.
    // artifactDaysToKeepStr: artifacts are only kept up to this many days.
    // artifactNumToKeepStr: only this many builds have their artifacts kept.
    buildDiscarder(logRotator(numToKeepStr: "1"))
    // Enable timestamps in build log console
    timestamps()
    // Maximum time to run the whole pipeline before canceling it
    timeout(time: 1, unit: 'HOURS')
    // Use Jenkins ANSI Color Plugin for log console
    ansiColor('xterm')
    // Limit build concurrency to 1 per branch
    disableConcurrentBuilds()
  }
  stages
  {
    stage('Prepare') {
      steps {
          copyArtifacts(projectName: 'gitea-sync/ant-http/master', target: 'antd');
          sh'''
          make clean || true
          rm -rf build/* || true
          mkdir build || true
          '''
      }
    }
    stage('Build AMD64') {
      steps {
        script{
          env.arch = "amd64"
        }
        build_plugin()
      }
    }
    stage('Build ARM64') {
      steps {
        script{
          env.arch = "arm64"
        }
        build_plugin()
      }
    }
    stage('Build ARM') {
      steps {
        script{
          env.arch = "arm"
        }
        build_plugin()
      }
    }
    stage("Archive") {
      steps{
        script {
            archiveArtifacts artifacts: 'build/', fingerprint: true, onlyIfSuccessful: true
        }
      }
    }
  }
}