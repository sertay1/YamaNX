package com.example.yamanx.data

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

data class Patch(
    val name: String,
    val titleId: String,
    val url: String,
    val yapimci: String,
    val patchVersion: String,
    var size: String
) {
    var isInstalled by mutableStateOf(false)
    var gameInstalled by mutableStateOf(false)

    val normalizedName: String
        get() = name.lowercase()
}
