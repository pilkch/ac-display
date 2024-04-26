// https://eslint.org/docs/latest/use/configure/configuration-files#using-predefined-configurations

import js from "@eslint/js";
import eslintPluginJsonc from 'eslint-plugin-jsonc';

export default [
  {
    languageOptions: {
      globals: {
        ...globals.browser
      }
    }
  },
  js.configs.recommended,
  ...eslintPluginJsonc.configs['flat/recommended-with-jsonc'],
  {
    rules: {
      // override/add rules settings here, such as:
      // 'jsonc/rule-name': 'error'
    }
  }
];
