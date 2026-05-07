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
