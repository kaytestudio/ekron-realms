// Ekron Realms - Jenkins pipeline: builds the Docker image (CMake build +
// CTest + .deb, see Dockerfile) and archives the products from /app/dist.
pipeline {
  agent any

  stages {
    stage('Checkout') {
      steps {
        checkout scm
      }
    }

    stage('Build with Docker') {
      steps {
        sh 'docker build -t ekron-builder .'
      }
    }

    stage('Extract Binaries') {
      steps {
        sh 'mkdir -p output && docker run --rm -v "$PWD/output:/output" ekron-builder cp -r /app/dist/. /output/'
      }
    }
  }

  post {
    success {
      archiveArtifacts artifacts: 'output/**/*', fingerprint: true
    }
  }
}
