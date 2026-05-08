module.exports = [
  {
    "type": "heading",
    "defaultValue": "Settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Appearance"
      },
      {
        "type": "radiogroup",
        "messageKey": "THEME",
        "label": "Theme",
        "defaultValue": "0",
        "options": [
          { "label": "Dark",  "value": "0" },
          { "label": "Light", "value": "1" }
        ]
      },
      {
        "type": "toggle",
        "messageKey": "ANIMATE_SECONDS",
        "label": "Animate seconds",
        "description": "Trace the line around the time box once per minute to show the seconds.",
        "defaultValue": true
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Bottom Bar"
      },
      {
        "type": "color",
        "messageKey": "BOTTOM_BG_COLOR",
        "label": "Background color",
        "defaultValue": "0x000000",
        "sunlight": false
      },
      {
        "type": "color",
        "messageKey": "BOTTOM_FG_COLOR",
        "label": "Text color",
        "defaultValue": "0xFFFFFF",
        "sunlight": false
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Activity"
      },
      {
        "type": "input",
        "messageKey": "STEP_GOAL",
        "label": "Daily step goal",
        "defaultValue": "10000",
        "attributes": {
          "type": "number",
          "min": "0",
          "step": "500"
        }
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
