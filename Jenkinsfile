def build_plugin()
{
  sh '''
  set -e
  cd $WORKSPACE
  mkdir -p build/$arch/opt/www
  [ -f Makefile ] && (make clean || true)
  libtoolize
  aclocal
  autoconf
  automake --add-missing
  search_path=$(realpath antd/build/$arch/usr)
  CFLAGS="-I$search_path/include" LDFLAGS="-L$search_path/lib" ./configure  --prefix=/opt/www
  CFLAGS="-I$search_path/include" LDFLAGS="-L$search_path/lib" make
  DESTDIR=$WORKSPACE/build/$arch make install
  '''
}

pipeline{
  agent { node{ label'master' }}
  options {
    buildDiscarder(logRotator(numToKeepStr: "1"))
    // Enable timestamps in build log console
    timestamps()
    // Maximum time to run the whole pipeline before canceling it
    timeout(time: 3, unit: 'HOURS')
    // Use Jenkins ANSI Color Plugin for log console
    ansiColor('xterm')
    // Limit build concurrency to 1 per branch
    disableConcurrentBuilds()
  }
  stages
  {
    stage('Prepare dependencies')
    {
      steps {
         copyArtifacts(projectName: 'gitea-sync/ant-http/master', target: 'antd');
      }
    }
    stage('Build AMD64') {
      agent {
          docker {
              image 'xsangle/ci-tools:bionic-amd64'
              reuseNode true
          }
      }
      steps {
        script{
          env.arch = "amd64"
        }
        build_plugin()
      }
    }
    stage('Build ARM64') {
      agent {
          docker {
              image 'xsangle/ci-tools:bionic-arm64'
              reuseNode true
          }
      }
      steps {
        script{
          env.arch = "arm64"
        }
        build_plugin()
      }
    }
    stage('Build ARM') {
      agent {
          docker {
              image 'xsangle/ci-tools:bionic-arm'
              reuseNode true
          }
      }
      steps {
        script{
          env.arch = "arm"
        }
        build_plugin()
      }
    }
    stage('Archive') {
      steps {
        script {
            archiveArtifacts artifacts: 'build/', fingerprint: true
        }
      }
    }
  }
}
