<!--
Copyright (C) 2024 Checkmk GmbH - License: GNU General Public License v2
This file is part of Checkmk (https://checkmk.com). It is subject to the terms and
conditions defined in the file COPYING, which is part of this source code package.
-->
<script setup lang="ts">
import QuickSetup from '@/quick-setup/QuickSetup.vue'

export interface QuickSetupAppProperties {
  /** @property {string} quick_setup_id - The quick setup id  */
  quick_setup_id: string
}

defineProps<QuickSetupAppProperties>()
</script>

<template>
  <QuickSetup :quick_setup_id="quick_setup_id" />
</template>

<style>
@import '@/assets/variables.css';
@import './variables.css';
</style>
