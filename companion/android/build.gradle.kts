plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.compose) apply false
}

// The project sits in OneDrive, which locks files while they are being written,
// so build output goes to the user's home folder instead (as the firmware's does).
allprojects {
    layout.buildDirectory.set(File(System.getProperty("user.home"), ".orbit-build-app/${project.name}"))
}
