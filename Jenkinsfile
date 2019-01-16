pipeline {
    agent {
        kubernetes {
            label 'mypod'
            defaultContainer 'jnlp'
            yamlFile 'pod.yaml'
        }
    }
    stages {
        stage('Checkout') {
            steps {
                container('coypullvm') {
                    sh 'mkdir -p build'
                    sh '''
                  export LD_LIBRARY_PATH=/usr/local/lib/ &&
                  mkdir libs &&
                  cd libs &&
                  git clone https://github.com/Tencent/rapidjson.git &&
                  cd .. &&
                  cd build &&
                  cmake -D CMAKE_BUILD_TYPE=Debug -D BUILD_TESTING=ON .. &&
                  make coyputest &&
                  ./coyputest --gtest_output="xml:testresults.xml"
                '''
                }
            }
        }
        stage ('test') {
            steps {
                container('coypullvm') {
                    junit 'build/testresults.xml'
                }
            }
        }
    }
}
