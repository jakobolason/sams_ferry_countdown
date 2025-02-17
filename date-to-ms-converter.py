from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

def date_to_ms_since_epoch(date_str, timezone='Europe/Copenhagen'):
    """
    Convert a date string to milliseconds since epoch, accounting for DST
    
    :param date_str: Date in format 'dd-mm-yyyy:hh:mm'
    :param timezone: Timezone to use for DST calculation
    :return: Milliseconds since epoch
    """
    # Parse the date string
    dt = datetime.strptime(date_str, '%d-%m-%Y:%H:%M')
    print("timezone:_ ", timezone)
    # Set the timezone
    tz = ZoneInfo(timezone)
    localized_dt = dt.replace(tzinfo=tz)
    
    # Check if it's during daylight saving time
    is_dst = localized_dt.tzinfo.dst(localized_dt) != timedelta(0)
    
    # Convert to milliseconds since epoch
    ms_since_epoch = int(localized_dt.timestamp() * 1000)
    
    return {
        'ms_since_epoch': ms_since_epoch,
        'is_dst': is_dst
    }

# Example usage
if __name__ == '__main__':
    # Test cases
    test_dates = [
        '15-02-2025:12:00',  # Summer time
        '27-01-2025:12:00'   # Winter time
    ]
    
    for date in test_dates:
        result = date_to_ms_since_epoch(date, "Europe/Copenhagen")
        print(f"Date: {date}")
        print(f"Milliseconds since epoch: {result['ms_since_epoch']}")
        print(f"Is during DST: {result['is_dst']}")
        print()
